#include "AudioBuffer.h"

#include <QDebug>

#include <math.h>

void AudioBuffer::appendBytes(const QByteArray &bytes)
{
    m_sendBuffer.append(bytes);

    qDebug() << "Sending" << bytes.toHex(' ');

    const int framesPerBit = sampleRate / baud; // idklol, i think this is right
    QVector<float> newAudio(m_sendBuffer.count() * 10 * framesPerBit);

    m_bitNum = 10; // im lazy, > 9 makes advance() take the next byte
    m_time = 0.;
    for (int i=0; i<newAudio.size(); i += framesPerBit) {
        if (!advance()) {
            qWarning() << "audio buffer too big" << i;
            break;
        }
        generateSound(&newAudio.data()[i], framesPerBit);
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
        return;
    }
    const double advance = double(freq) / sampleRate;
    for (size_t i=0; i<frames; i++) {
        output[i] = sin(TWO_PI * m_time);
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
    //qDebug() << m_sendBuffer.count() << m_bitNum << m_currentTone;
    m_bitNum++;
    return true;
}

void AudioBuffer::takeFrames(int frameCount, void *output)
{
    const size_t byteCount = sizeof(float) * frameCount;
    bzero(output, byteCount); // could Optimize™ and only zero the frames at the end, but idc
    if (frameCount > m_audio.size()) {
        frameCount = m_audio.size();
    }

    memcpy(output, m_audio.data(), byteCount);
    m_audio.remove(0, frameCount);
}
