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
    enum Waveform {
        Invalid = -1,
        Square,
        Sawtooth,
        Triangle,
        Sine,
        WaveformCount
    };
    Waveform waveform = Triangle;

    int frameCount() const { return m_audio.size(); }
    void takeFrames(uint32_t frameCount, void *output);
    bool isEmpty() const { return m_audio.isEmpty(); }
    void appendBytes(const QByteArray &bytes);
    bool saveWavFile(const QString &filename);
    void clear() { m_audio.clear(); }

    int channels = 1;
    int sampleRate = 44100;
    int baud = 300;

    int spaceFrequency = 2025;
    int markFrequency = 2225;

    float volume = 0.1;

private:
    enum Tone {
        OriginatingMark,
        OriginatingSpace,
        AnsweringMark,
        AnsweringSpace,
        Silence,
        ToneCount
    };

    inline int frequency(const Tone tone) {
        switch(tone) {
        case Tone::OriginatingMark: return 1270;
        case Tone::OriginatingSpace: return 1070;
        case Tone::AnsweringMark: return spaceFrequency;//2225;
        case Tone::AnsweringSpace: return markFrequency;//2025;
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
