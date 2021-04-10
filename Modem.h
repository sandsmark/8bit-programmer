#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QDebug>

#include <memory>
#include <mutex>

struct ma_device;
struct ma_device_info;
struct ma_context;
struct ma_waveform;

class Modem : public QObject
{
    Q_OBJECT
public:
    enum Tone {
        OriginatingMark,
        OriginatingSpace,
        AnsweringMark,
        AnsweringSpace,
        Silence,
        ToneCount
    };
    Q_ENUM(Tone)

    explicit Modem(QObject *parent);
    ~Modem();

    bool initAudio(const QString &deviceName = "");

    bool audioAvailable() const { return m_maContext != nullptr; }
    bool isInitialized() const { return m_device != nullptr; }

    QStringList audioOutputDevices();

public slots:
    void send(const QByteArray &bytes);
    void sendHex(const QByteArray &bytes);
    void stop();
    void setAudioDevice(const QString &name) { qDebug() << "Setting" << name;  initAudio(name); }

signals:
    void finished();

private:
    static inline int frequency(const Tone tone) {
        switch(tone) {
        case Tone::OriginatingMark: return 1270;
        case Tone::OriginatingSpace: return 1070;
        case Tone::AnsweringMark: return 2225;
        case Tone::AnsweringSpace: return 2025;
        default: return 440;
        }
    }

    void generateSound(void *output, size_t bytes);
    void maybeAdvance();

    static void miniaudioCallback(ma_device* device, void *output, const void *input, uint32_t frameCount);

    QByteArray m_sendBuffer;
    uint8_t m_currentByte = 0;
    uint8_t m_bitNum = 0;

    int m_sampleRate = 44100;
    uint32_t m_timeIndex = 0; // idk should do math so it doesn't wrap randomly

    Tone m_currentTone = Silence;

    std::unique_ptr<ma_context> m_maContext;
    std::unique_ptr<ma_device> m_device;
    std::array<std::unique_ptr<ma_waveform>, ToneCount> m_waveforms;
    float m_volume = 0.2f;

    QElapsedTimer m_clock;

    // tsan doesn't support qmutex, unfortunately, so we eat the sour apple and use the ugly std APIs
    std::recursive_mutex m_maMutex;

    QHash<QString, std::shared_ptr<ma_device_info>> m_devices;
    QString m_currentDevice;
};

