#pragma once

#include <QObject>

#include <memory>

struct ma_device;
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

    explicit Modem(QObject *parent = nullptr);
    ~Modem();

    bool initAudio();

signals:

private:
    static inline int frequency(const Tone tone) {
        switch(tone) {
        case Tone::OriginatingMark: return 1270;
        case Tone::OriginatingSpace: return 1070;
        case Tone::AnsweringMark: return 2225;
        case Tone::AnsweringSpace: return 2025;
        default: return 0;
        }
    }

    void generateSound(void *output, size_t bytes);

    static void miniaudioCallback(ma_device* device, void *output, const void *input, uint32_t frameCount);

    QByteArray m_sendBuffer;
    uint8_t m_currentByte = 0;
    uint8_t m_bitNum = 0;

    int m_sampleRate = 44100;
    uint32_t m_timeIndex = 0; // idk should do math so it doesn't wrap randomly

    Tone m_currentTone = Silence;

    std::unique_ptr<ma_device> m_device;
    std::array<std::unique_ptr<ma_waveform>, ToneCount> m_waveforms;
    float m_volume = 0.2f;
};

