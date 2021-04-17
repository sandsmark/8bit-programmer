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

struct AudioBuffer
{
    enum Encoding {
        // Just one for now, hardcoded start/stop bits
        Ascii8N1
    };
    Encoding m_encoding = Ascii8N1;

    void takeFrames(int frameCount, void *output);
    bool isEmpty() const { return m_audio.isEmpty(); }
    void appendBytes(const QByteArray &bytes);

    int m_sampleRate = 44100;

private:
    enum Tone {
        OriginatingMark,
        OriginatingSpace,
        AnsweringMark,
        AnsweringSpace,
        Silence,
        ToneCount
    };

    static inline int frequency(const Tone tone) {
        switch(tone) {
        case Tone::OriginatingMark: return 1270;
        case Tone::OriginatingSpace: return 1070;
        case Tone::AnsweringMark: return 2225;
        case Tone::AnsweringSpace: return 2025;
        default: return 1270; // Mark default when no signal
        }
    }

    bool advance();
    void generateSound(float *output, size_t frames);

    double m_time = 0.; // idk should do math so it doesn't wrap randomly
    QByteArray m_sendBuffer;
    uint8_t m_currentByte = 0;
    uint8_t m_bitNum = 0;

    Tone m_currentTone = Silence;

    QVector<float> m_audio;
};

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

public slots:
    void send(const QByteArray &bytes);
    void sendHex(const QByteArray &bytes);
    void stop();
    void setAudioDevice(const QString &name) { qDebug() << "Setting" << name;  initAudio(name); }

signals:
    void finished();

private:

    static void miniaudioCallback(ma_device* device, void *output, const void *input, uint32_t frameCount);

    std::unique_ptr<ma_context> m_maContext;
    std::unique_ptr<ma_device> m_device;
    std::unique_ptr<ma_waveform> m_waveform;
    float m_volume = 1.0f;

    QElapsedTimer m_clock;

    // tsan doesn't support qmutex, unfortunately, so we eat the sour apple and use the ugly std APIs
    std::recursive_mutex m_maMutex;

    QHash<QString, std::shared_ptr<ma_device_info>> m_devices;
    QString m_currentDevice;

    AudioBuffer m_buffer;

};

