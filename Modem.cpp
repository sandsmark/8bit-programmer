#include "Modem.h"
#include <QDebug>

#include <cmath>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_JACK
#define MA_NO_SDL
#define MA_NO_OPENAL
#include <miniaudio/miniaudio.h>


#define DEVICE_FORMAT       ma_format_f32
#define DEVICE_CHANNELS     2
#define DEVICE_SAMPLE_RATE  48000

Modem::Modem(QObject *parent) : QObject(parent)
{

}

Modem::~Modem()
{
    if (m_device) {
        ma_device_uninit(m_device.get());
    }
}

bool Modem::initAudio()
{
    if (m_device) {
        ma_device_uninit(m_device.get());
    }
    m_device = std::make_unique<ma_device>();

    ma_device_config deviceConfig;
    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = DEVICE_FORMAT;
    deviceConfig.playback.channels = DEVICE_CHANNELS;
    deviceConfig.sampleRate        = DEVICE_SAMPLE_RATE;
    deviceConfig.dataCallback      = &Modem::miniaudioCallback;
    deviceConfig.pUserData         = this;


    if (ma_device_init(nullptr, &deviceConfig, m_device.get()) != MA_SUCCESS) {
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
                    m_volume,
                    frequency(Tone(i))
        );

        m_waveforms[i] = std::make_unique<ma_waveform>();
        ma_waveform_init(&config, m_waveforms[i].get());
    }

    return true;
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

void Modem::miniaudioCallback(ma_device *device, void *output, const void *input, uint32_t frameCount)
{
    Q_UNUSED(input);

    Modem *that = reinterpret_cast<Modem*>(device->pUserData);
    ma_waveform_read_pcm_frames(that->m_waveforms[that->m_currentTone].get(), output, frameCount);
}
