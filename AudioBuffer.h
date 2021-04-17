#pragma once

#include <QVector>
#include <QByteArray>

#include <cstddef>

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

    int sampleRate = 44100;
    int baud = 300;

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
