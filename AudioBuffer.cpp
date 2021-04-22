#include "AudioBuffer.h"

#include <QDebug>

#include <math.h>

// Start and end with carrier, since soundcards have a tendency to be noisy when starting/stopping
static constexpr int s_carrierPrefix = 100; // Prefix with 100 bits of carrier tone
static constexpr int s_carrierSuffix = 100; // End with 100 bits of carrier tone

void AudioBuffer::appendBytes(const QByteArray &bytes)
{
    m_sendBuffer.append(bytes);

    const int framesPerBit = sampleRate / baud; // idklol, i think this is right
    const int prefixLength = s_carrierPrefix * framesPerBit;
    const int suffixLength = s_carrierSuffix * framesPerBit;
    QVector<float> newAudio(m_sendBuffer.count() * 10 * framesPerBit + prefixLength + suffixLength);

    qDebug() << "Prefix frame length" << prefixLength << "frames per bit" << framesPerBit;

    m_bitNum = 10; // im lazy, > 9 makes advance() take the next byte
    m_time = 0.;

    m_currentTone = AnsweringMark; // Carrier is the mark
    int position = 0;
    for (position=0; position<prefixLength; position += framesPerBit) {
        generateSound(&newAudio.data()[position], framesPerBit);
    }

    for (; position<newAudio.size() - suffixLength; position += framesPerBit) {
        if (!advance()) {
            qWarning() << "audio buffer too big" << position;
            break;
        }
        generateSound(&newAudio.data()[position], framesPerBit);
    }

    m_currentTone = AnsweringMark; // Keep some carrier, less abrupt end
    for (; position<newAudio.size(); position += framesPerBit) {
        generateSound(&newAudio.data()[position], framesPerBit);
    }

    m_audio.append(newAudio);

    if (!m_sendBuffer.isEmpty()) {
        qWarning() << "Audio buffer too small";
        qDebug() << m_sendBuffer.size() << m_audio.size();
    }
}

#define TWO_PI (M_PI * 2.)

void AudioBuffer::generateSound(float *output, size_t frames)
{
    const int freq = frequency(m_currentTone);
    if (!freq || ! sampleRate) {
        qWarning() << "missing frequency or sample rate" << freq << sampleRate;
        return;
    }
    const double advance = double(freq) / sampleRate;
    for (size_t i=0; i<frames; i++) {
        // square
        //if (m_time - uint64_t(m_time) < 0.5) {
        //    output[i] = volume;
        //} else {
        //    output[i] = -volume;
        //}
        output[i] = sin(TWO_PI * m_time) * volume;
        m_time += advance;
    }
}

bool AudioBuffer::advance()
{
    if (m_bitNum > 9) {
        if (m_sendBuffer.isEmpty()) {
            qDebug() << "Finito";
            return false;
        }

        m_currentByte = m_sendBuffer[0];
        m_sendBuffer.remove(0, 1);
        m_bitNum = 0;
    }

    if (m_bitNum > 8) {
        switch(m_encoding) {
        case Ascii8N1:
            m_currentTone = AnsweringMark;
            break;
        default:
            qWarning() << "Invalid encoding";
            m_currentTone = AnsweringMark;
            break;
        }
    } else if (m_bitNum == 0) {
        switch(m_encoding) {
        case Ascii8N1:
            m_currentTone = AnsweringSpace;
            break;
        default:
            qWarning() << "Invalid encoding";
            m_currentTone = AnsweringSpace;
            break;
        }
    } else {
        m_currentTone = (m_currentByte >> (m_bitNum - 1)) & 0b1 ? AnsweringMark : AnsweringSpace;
    }
    //qDebug() << m_sendBuffer.count() << m_bitNum << m_currentTone;
    m_bitNum++;
    return true;
}

void AudioBuffer::takeFrames(int frameCount, void *output)
{
    const size_t byteCount = sizeof(float) * frameCount;
    memset(output, '\0', byteCount); // could Optimize™ and only zero the frames at the end, but idc
    if (frameCount > m_audio.size()) {
        frameCount = m_audio.size();
    }

    memcpy(output, m_audio.data(), byteCount);
    m_audio.remove(0, frameCount);
}
