#include "Modem.h"

#include "AudioBuffer.h"

#include <QDebug>
#include <cmath>
#include <QThread>
#include <QCoreApplication>

#define MA_NO_JACK
#define MA_NO_SDL
#define MA_NO_OPENAL
#include <miniaudio/miniaudio.h>

#define DEVICE_FORMAT       ma_format_f32
#define DEVICE_CHANNELS     1
#define DEVICE_SAMPLE_RATE 48000

Modem::Modem(QObject *parent) : QObject(parent)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    m_buffer = std::make_unique<AudioBuffer>();
    m_maContext = std::make_unique<ma_context>();
    ma_result ret = ma_context_init(nullptr, 0, nullptr, m_maContext.get());
    if (ret != MA_SUCCESS) {
        m_maContext.reset();
        return;
    }
    updateAudioDevices();

    connect(this, &Modem::finished, this, &Modem::stop, Qt::QueuedConnection);
}

Modem::~Modem()
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    if (m_device) {
        ma_device_uninit(m_device.get());
    }
    if (m_maContext) {
        ma_context_uninit(m_maContext.get());
    }

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
}

bool Modem::initAudio(const QString &deviceName)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);

    if (m_currentDevice == deviceName && m_device) {
        return true;
    }

    if (!m_maContext) {
        qWarning() << "No context available";
        return false;
    }

    if (m_device) {
        ma_device_uninit(m_device.get());
    }
    m_device = std::make_unique<ma_device>();

    ma_device_config deviceConfig;
    deviceConfig = ma_device_config_init(ma_device_type_playback);

    if (!deviceName.isEmpty() && m_devices.contains(deviceName)) {
        deviceConfig.playback.pDeviceID = &m_devices[deviceName]->id;
        qDebug() << "Using device" << m_devices[deviceName]->name;
    }

    deviceConfig.playback.format   = DEVICE_FORMAT;
    deviceConfig.playback.channels = DEVICE_CHANNELS;
    deviceConfig.sampleRate        = DEVICE_SAMPLE_RATE;
    deviceConfig.dataCallback      = &Modem::miniaudioCallback;
    deviceConfig.pUserData         = this;

    if (ma_device_init(m_maContext.get(), &deviceConfig, m_device.get()) != MA_SUCCESS) {
        qWarning() << "Failed to init device";
        return false;
    }

    m_buffer->sampleRate = deviceConfig.sampleRate;

    qDebug() << "Got device" << m_device->playback.name << "sample rate" << m_buffer->sampleRate;

    m_clock.start();

    m_currentDevice = deviceName;

    return true;
}
void Modem::send(const QByteArray &bytes)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    std::unique_lock<std::recursive_mutex> lock(m_maMutex);
    if (!m_device) {
        qWarning() << "No device available, refusing to fill buffer";
        return;
    }

    const bool wasEmpty = m_buffer->isEmpty();
    for (int i=0; i<bytes.count(); i++) {
        m_buffer->appendBytes(bytes.mid(i, 1));
    }
//    m_buffer->appendBytes(bytes.mid(0, 2));

    lock.unlock();
    if (wasEmpty && !ma_device_is_started(m_device.get())) {
        ma_device_start(m_device.get());
    }
}

void Modem::sendHex(const QByteArray &encoded)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
    send(QByteArray::fromHex(encoded));
}

void Modem::stop()
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    ma_device_stop(m_device.get());
}

QStringList Modem::audioOutputDevices()
{
    return m_outputDeviceList;
}

void Modem::updateAudioDevices()
{
    // Can only run from one thread, and cannot be run while audio is playing

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
    m_devices.clear();
    m_outputDeviceList.clear();

    if (!m_maContext) {
        qWarning() << "Audio not available";
        return;
    }

    ma_device_info* devicesInfo;
    ma_uint32 devicesCount;
    ma_result ret = ma_context_get_devices(m_maContext.get(), &devicesInfo, &devicesCount, nullptr, nullptr);
    if (ret != MA_SUCCESS) {
        qWarning() << "Failed to get list of devices";
        return;
    }

    QString defaultDevice;
    for (size_t i=0; i<devicesCount; i++) {
        const QString name = QString::fromLocal8Bit(devicesInfo[i].name);
        if (devicesInfo[i].isDefault) {
            m_outputDeviceList.prepend(name);
        } else {
            m_outputDeviceList.append(name);
        }
        m_devices[name] = std::make_shared<ma_device_info>(devicesInfo[i]);
    }
    emit devicesUpdated(m_outputDeviceList);
}

void Modem::setBaud(const int baud)
{
    if (baud <= 0) {
        return;
    }
    m_buffer->baud = baud;
}

void Modem::setSampleRate(const int /*rate*/)
{
    // TODO, need to validate and shit
}

void Modem::setFrequencies(const int space, const int mark)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    m_buffer->spaceFrequency = space;
    m_buffer->markFrequency = mark;
}

void Modem::setVolume(const float volume)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    static constexpr qreal loudnessToVoltage = qreal(0.67);
    m_buffer->volume = qBound(0., pow(volume, loudnessToVoltage) , 1.);
}

AudioBuffer::Waveform Modem::currentWaveform() const
{
    return m_buffer->waveform;
}

void Modem::setWaveform(int waveform)
{
    if (waveform <= AudioBuffer::Invalid || waveform >= AudioBuffer::WaveformCount) {
        qWarning() << "Invalid waveform" << waveform << "defaulting to triangle";
        waveform = AudioBuffer::Triangle;
    }
    m_buffer->waveform = AudioBuffer::Waveform(waveform);
}

void Modem::miniaudioCallback(ma_device *device, void *output, const void *input, uint32_t frameCount)
{
    Q_UNUSED(input);

    Modem *that = reinterpret_cast<Modem*>(device->pUserData);
    std::lock_guard<std::recursive_mutex> lock(that->m_maMutex);

    that->m_buffer->takeFrames(frameCount, output);
    if (that->m_buffer->isEmpty()) {
        emit that->finished();
    }
}
