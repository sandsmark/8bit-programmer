#pragma once

#include "AudioBuffer.h"

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QDebug>

#include <memory>
#include <mutex>

struct ma_device;
struct ma_device_info;
struct ma_context;

class Modem : public QObject
{
    Q_OBJECT
public:
    explicit Modem(QObject *parent);
    ~Modem();

    bool initAudio(const QString &deviceName = "");

    bool audioAvailable() const { return m_maContext != nullptr; }
    bool isInitialized() const { return m_device != nullptr; }

    QStringList audioOutputDevices();

    void setBaud(const int baud);
    void setSampleRate(const int rate);
    void setFrequencies(const int space, const int mark);
    void setVolume(const float volume);
    void setWaveform(int waveform);
    AudioBuffer::Waveform currentWaveform() const;

    bool isActive() const { return m_isActive; }

public slots:
    void send(const QByteArray &bytes);
    void sendHex(const QByteArray &bytes);
    void stop();
    void setAudioDevice(const QString &name) { qDebug() << "Setting" << name;  initAudio(name); }
    void updateAudioDevices();

signals:
    void finished();
    void devicesUpdated(const QStringList devices);

private:
    static void miniaudioCallback(ma_device* device, void *output, const void *input, uint32_t frameCount);

    std::unique_ptr<ma_context> m_maContext;
    std::unique_ptr<ma_device> m_device;
    float m_volume = 1.0f;

    QElapsedTimer m_clock;

    // tsan doesn't support qmutex, unfortunately, so we eat the sour apple and use the ugly std APIs
    std::recursive_mutex m_maMutex;

    QHash<QString, std::shared_ptr<ma_device_info>> m_devices;
    QString m_currentDevice;

    std::unique_ptr<AudioBuffer> m_buffer;

    QStringList m_outputDeviceList;
    bool m_isActive = false;
};

