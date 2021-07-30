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

#define DEFAULT_FORMAT       ma_format_f32
#define DEFAULT_SAMPLERATE  44100

Modem::Modem(QObject *parent) : QObject(parent),
    m_device(nullptr, &Modem::freeDevice)
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

    if (m_maContext) {
        ma_context_uninit(m_maContext.get());
    }

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
}

static bool getDeviceInfo(ma_context *context, const QByteArray &name, ma_device_info *info)
{
    ma_device_info* devicesInfo;
    ma_uint32 devicesCount;
    if (ma_context_get_devices(context, &devicesInfo, &devicesCount, nullptr, nullptr) != MA_SUCCESS) {
        return false;
    }

    for (size_t i=0; i<devicesCount; i++) {
        if (devicesInfo[i].name != name) {
            continue;
        }
        if (ma_context_get_device_info(context, ma_device_type_playback, &devicesInfo[i].id, ma_share_mode_shared, info) == MA_SUCCESS) {
            return true;
        }
        // Idk, maybe there are more with the same name?
//        return false;
    }

    return false;
}

#if 0 // we're not supposed to use nativeDataFormats
static void findBestFormat(const ma_device_info &info, uint32_t *sampleRate, ma_format *sampleFormat)
{
    for (uint32_t i=0; i<info.nativeDataFormatCount; i++) {
        if (info.nativeDataFormats[i].sampleRate < *sampleRate) {
            continue;
        }
        *sampleRate = info.nativeDataFormats[i].sampleRate;
        *sampleFormat = info.nativeDataFormats[i].format;
    }
}
#endif

void Modem::freeDevice(ma_device *dev)
{
    if (!dev) {
        return;
    }
    ma_device_uninit(dev);
    delete dev;
}

bool Modem::initAudio(const QString &deviceName)
{
    if (!m_outputDeviceList.contains(deviceName)) {
        qWarning() << "Invalid device" << deviceName;
        return false;
    }
    qDebug() << "Initing audio to" << deviceName;
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);

    if (!audioAvailable()) {
        qDebug() << "Audio not available!";
        return false;
    }

    if (m_currentDevice == deviceName && m_device) {
        return true;
    }

    if (!m_maContext) {
        qWarning() << "No context available";
        return false;
    }

    m_device.reset(new ma_device);
    m_device->onStop = &Modem::maStoppedCallback;

    ma_device_config deviceConfig;
    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.channels = 1;
    deviceConfig.playback.format   = DEFAULT_FORMAT;
    deviceConfig.sampleRate        = DEFAULT_SAMPLERATE;

    ma_device_info deviceInfo;
    if (!deviceName.isEmpty() && getDeviceInfo(m_maContext.get(), deviceName.toLocal8Bit(), &deviceInfo)) {
        deviceConfig.playback.pDeviceID = &deviceInfo.id;
//        if (deviceInfo.formatCount > 0) {
//            deviceConfig.playback.format = deviceInfo.formats[0];
//        }

        deviceConfig.sampleRate = deviceInfo.maxSampleRate;

        qDebug() << "Device:" << deviceInfo.name << "min sample rate" << deviceInfo.minSampleRate << "max sample rate" << deviceInfo.maxSampleRate << "Formats:" << deviceInfo.formatCount;
    }

    deviceConfig.dataCallback      = &Modem::maDataCallback;
    deviceConfig.stopCallback      = &Modem::maStoppedCallback;
    deviceConfig.pUserData         = this;

    if (ma_device_init(m_maContext.get(), &deviceConfig, m_device.get()) != MA_SUCCESS) {
        qWarning() << "Failed to init device";
        return false;
    }

    m_buffer->sampleRate = int(deviceConfig.sampleRate);

    qDebug() << "Got device" << m_device->playback.name << "sample rate" << m_buffer->sampleRate << "format" << ma_get_format_name(m_device->playback.format);

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
    m_framesToSend = m_buffer->frameCount();

    lock.unlock();
    if (wasEmpty && !ma_device_is_started(m_device.get())) {
        ma_device_start(m_device.get());
    }
    m_isActive = true;
}

void Modem::sendHex(const QByteArray &encoded)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    send(QByteArray::fromHex(encoded));
}

void Modem::stop()
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    ma_device_stop(m_device.get());

    m_isActive = false;

    emit stopped();

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
    m_buffer->clear();
}

QStringList Modem::audioOutputDevices()
{
    return m_outputDeviceList;
}

void Modem::updateAudioDevices()
{
    // Can only run from one thread, and cannot be run while audio is playing

    std::lock_guard<std::recursive_mutex> lock(m_maMutex);

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

    m_outputDeviceList.clear();

    for (size_t i=0; i<devicesCount; i++) {
        const QString name = QString::fromLocal8Bit(devicesInfo[i].name);
        if (devicesInfo[i].isDefault) {
            m_outputDeviceList.prepend(name);
        } else {
            m_outputDeviceList.append(name);
        }
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
    if (space <= 0 || mark <= 0) {
        qWarning() << "Invalid frequencies" << space << mark;
        return;
    }

    m_buffer->spaceFrequency = space;
    m_buffer->markFrequency = mark;
}

void Modem::setVolume(const float volume)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    static constexpr double loudnessToVoltage = qreal(0.67);
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

void Modem::maDataCallback(ma_device *device, void *output, const void *input, uint32_t frameCount)
{
    Q_UNUSED(input);

    Modem *that = reinterpret_cast<Modem*>(device->pUserData);
    std::lock_guard<std::recursive_mutex> lock(that->m_maMutex);

    that->m_buffer->takeFrames(frameCount, output);

    if (that->m_buffer->isEmpty()) {
        emit that->progress(100);
        emit that->finished();
    } else {
        emit that->progress(100 - (100 * that->m_buffer->frameCount() / that->m_framesToSend));
    }
}

// Does not seem to get called
void Modem::maStoppedCallback(ma_device *device)
{
    Q_UNUSED(device);
#if 0
    Modem *that = reinterpret_cast<Modem*>(device->pUserData);
    qDebug() << "Stopped";
    emit that->stopped();
#endif
}
