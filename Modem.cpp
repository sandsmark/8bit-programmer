#include "Modem.h"
#include <QDebug>

#include <cmath>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_JACK
#define MA_NO_SDL
#define MA_NO_OPENAL
#include <miniaudio/miniaudio.h>


#define BAUDS 300

#define DEVICE_FORMAT       ma_format_f32
#define DEVICE_CHANNELS     1
#define DEVICE_SAMPLE_RATE  48000

Modem::Modem(QObject *parent) : QObject(parent)
{
    m_maContext = std::make_unique<ma_context>();
    ma_result ret = ma_context_init(nullptr, 0, nullptr, m_maContext.get());
    if (ret != MA_SUCCESS) {
        m_maContext.reset();
        return;
    }

    connect(this, &Modem::finished, this, &Modem::stop, Qt::QueuedConnection);

}

Modem::~Modem()
{
    std::lock_guard<std::recursive_mutex> lock(m_maMutex);

    if (m_device) {
        ma_device_uninit(m_device.get());
    }
    if (m_maContext) {
        ma_context_uninit(m_maContext.get());
    }
}

bool Modem::initAudio(const QString &deviceName)
{
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
    qDebug() << "Got device" << m_device->playback.name;


    for (int i=0; i<ToneCount; i++) {
        ma_waveform_config config = ma_waveform_config_init(
                    m_device->playback.format,
                    m_device->playback.channels,
                    m_device->sampleRate,
                    ma_waveform_type_sine,
                    i == Silence ? 0 : m_volume,
                    frequency(Tone(i))
        );

        m_waveforms[i] = std::make_unique<ma_waveform>();
        ma_waveform_init(&config, m_waveforms[i].get());
    }

    m_clock.start();

    m_currentDevice = deviceName;

    return true;
}

void Modem::send(const QByteArray &bytes)
{
    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
    if (!m_device) {
        qWarning() << "No device available, refusing to fill buffer";
        return;
    }

    const bool wasEmpty = m_sendBuffer.isEmpty();
    m_sendBuffer.append(bytes);

    qDebug() << "Sending" << bytes.toHex(' ');

    if (wasEmpty && !ma_device_is_started(m_device.get())) {
        ma_device_start(m_device.get());
    }
}

void Modem::sendHex(const QByteArray &encoded)
{
    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
    send(QByteArray::fromHex(encoded));
}

void Modem::stop()
{
    ma_device_stop(m_device.get());
}

QStringList Modem::audioOutputDevices()
{
    std::lock_guard<std::recursive_mutex> lock(m_maMutex);
    m_devices.clear();

    if (!m_maContext) {
        qWarning() << "Audio not available";
        return {};
    }

    ma_device_info* devicesInfo;
    ma_uint32 devicesCount;
    ma_result ret = ma_context_get_devices(m_maContext.get(), &devicesInfo, &devicesCount, nullptr, nullptr);
    if (ret != MA_SUCCESS) {
        qWarning() << "Failed to get list of devices";
        return {};
    }

    QStringList list;
    QString defaultDevice;
    for (size_t i=0; i<devicesCount; i++) {
        const QString name = QString::fromLocal8Bit(devicesInfo[i].name);
        if (devicesInfo[i].isDefault) {
            list.prepend(name);
        } else {
            list.append(name);
        }
        m_devices[name] = std::make_shared<ma_device_info>(devicesInfo[i]);
    }

    return list;
}

#define TWO_PI (M_PI * 2.)

void Modem::generateSound(void *output, size_t bytes)
{
    memset(output, 0, bytes);

    const int freq = frequency(m_currentTone);
    if (!freq || ! m_sampleRate) {
        return;
    }
    const float period = TWO_PI * freq / m_sampleRate;
    uint8_t *buffer = reinterpret_cast<uint8_t*>(output);
    for (size_t i=0; i<bytes; i++) {
        buffer[i] = sin(period * m_timeIndex++) * 127;
    }
}

void Modem::maybeAdvance()
{
    std::lock_guard<std::recursive_mutex> lock(m_maMutex);

    if (m_clock.elapsed() < 1000/BAUDS) {
        return;
    }

    m_bitNum++;
    if (m_bitNum > 9) {
        if (m_sendBuffer.isEmpty()) {
            emit finished();
            return;
        }

        m_currentByte = m_sendBuffer[0];
        m_sendBuffer.remove(0, 1);
        m_bitNum = 0;
    }
    if (m_bitNum > 8) {
        switch(m_encoding) {
        case Ascii8N1:
            m_currentTone = OriginatingMark;
            break;
        default:
            qWarning() << "Invalid encoding";
            m_currentTone = OriginatingMark;
            break;
        }
    } else if (m_bitNum == 0) {
        switch(m_encoding) {
        case Ascii8N1:
            m_currentTone = OriginatingSpace;
            break;
        default:
            qWarning() << "Invalid encoding";
            m_currentTone = OriginatingSpace;
            break;
        }
    } else {
        m_currentTone = (m_currentByte >> (m_bitNum - 1)) & 0b1 ? OriginatingMark : OriginatingSpace;
    }
    m_bitNum++;
}

void Modem::miniaudioCallback(ma_device *device, void *output, const void *input, uint32_t frameCount)
{
    Q_UNUSED(input);

    Modem *that = reinterpret_cast<Modem*>(device->pUserData);
    std::lock_guard<std::recursive_mutex> lock(that->m_maMutex);

    that->maybeAdvance();

    ma_waveform_read_pcm_frames(that->m_waveforms[that->m_currentTone].get(), output, frameCount);
}
