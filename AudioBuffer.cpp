#include "AudioBuffer.h"

#include <QDebug>
#include <QFile>

#include <math.h>

// Start and end with carrier, since soundcards have a tendency to be noisy when starting/stopping
static constexpr int s_carrierPrefix = 10; // Prefix with 100 bits of carrier tone
static constexpr int s_carrierSuffix = 10; // End with 100 bits of carrier tone

void AudioBuffer::appendBytes(const QByteArray &bytes)
{
    m_sendBuffer.append(bytes);
    qDebug() << "Using waveform" << waveform;

    const int samplesPerBit = sampleRate / baud; // idklol, i think this is right
    const int prefixLength = s_carrierPrefix * samplesPerBit;
    const int suffixLength = s_carrierSuffix * samplesPerBit;
    const int bitsPerByte = 1 + 8 + 1; // ASCII 8-N-1: 1 start bit, 8 bit data, 1 stop bit
    QVector<float> newAudio(m_sendBuffer.count() * bitsPerByte * samplesPerBit + prefixLength + suffixLength);

    // We fade in, since that seems to help avoiding the noise from the soundcard
    qDebug() << "Prefix frame length" << prefixLength << ", audio frames per bit:" << samplesPerBit << ", sample rate:" << sampleRate;
    qDebug() << "Advance for space:" <<  double(frequency(AnsweringSpace)) / sampleRate << "advance for mark" << double(frequency(AnsweringMark)) / sampleRate;

    m_bitNum = 10; // im lazy, > 9 makes advance() take the next byte

    printf("Bits: ");

    m_currentTone = AnsweringMark; // Carrier is the mark
    int position = 0;
    for (position=0; position<prefixLength; position += samplesPerBit) {
        generateSound(&newAudio.data()[position], samplesPerBit);
        printf("%d ", m_currentTone == AnsweringMark ? 1 : 0);
    }

    for (; position<newAudio.size() - suffixLength; position += samplesPerBit) {
        if (!advance()) {
            qWarning() << "audio buffer too big" << position;
            break;
        }
        generateSound(&newAudio.data()[position], samplesPerBit);

        printf("%d ", m_currentTone == AnsweringMark ? 1 : 0);
    }

    m_currentTone = AnsweringMark; // Keep some carrier, less abrupt end

    for (int it=samplesPerBit; position<newAudio.size(); position += samplesPerBit, it += samplesPerBit) {
        generateSound(&newAudio.data()[position], samplesPerBit);
        printf("%d ", m_currentTone == AnsweringMark ? 1 : 0);
    }
    puts("");

    m_audio.append(newAudio);

    if (!m_sendBuffer.isEmpty()) {
        qWarning() << "Audio buffer too small";
        qDebug() << m_sendBuffer.size() << m_audio.size();
    }

    saveWavFile("test.wav");
}

namespace {
    struct WavHeader {
        // RIFF header
        //uint32_t chunkID;
        const char chunkID[4] = {'R', 'I', 'F', 'F'};
        uint32_t chunkSize = 0;
        const char format[4] = {'W', 'A', 'V', 'E'};

        // fmt subchunk
        const char subchunk1ID[4] = {'f', 'm', 't', ' '};
        const uint32_t subchunk1Size =
            sizeof(audioFormat) +
            sizeof(numChannels) +
            sizeof(sampleRate) +
            sizeof(byteRate) +
            sizeof(blockAlign) +
            sizeof(bitsPerSample);

        enum AudioFormats {
            Invalid = 0x0,
            PCM = 0x1,
            ADPCM = 0x2,
            IEEEFloat = 0x3,
            ALaw = 0x6,
            MULaw = 0x7,
            DVIADPCM = 0x11,
            AAC = 0xff,
            WWISE = 0xffffu,
        };
        uint16_t audioFormat = Invalid;

        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;
        uint32_t byteRate = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 0;

        // data subchunk
        const char subchunk2ID[4] = {'d', 'a', 't', 'a'};
        uint32_t subchunk2Size = 0;

        bool isValid() const {
            return
                chunkSize &&
                audioFormat &&
                audioFormat &&
                numChannels &&
                sampleRate &&
                byteRate &&
                blockAlign &&
                bitsPerSample &&
                subchunk2Size;
        };
    };
} // namespace

bool AudioBuffer::saveWavFile(const QString &filename)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error "I can't be bothered to support big endian"
#endif

    if (m_audio.isEmpty()) {
        qWarning() << "No audio to save";
        return false;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open" << filename << "for writing";
        return false;
    }
    WavHeader header;
    if (std::is_floating_point<decltype(m_audio)::value_type>::value) {
        header.audioFormat = WavHeader::IEEEFloat;
    } else {
        header.audioFormat = WavHeader::PCM;
    }
    header.numChannels = channels;
    header.sampleRate = sampleRate;
    header.bitsPerSample = sizeof(decltype(m_audio)::value_type) * 8;
    header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
    header.blockAlign = header.numChannels * (header.bitsPerSample / 8);

    header.subchunk2Size = m_audio.size() * header.numChannels * (header.bitsPerSample / 8);
    header.chunkSize = sizeof(header.format) +
        (sizeof(header.subchunk1ID) + sizeof(header.subchunk1Size) + header.subchunk1Size) +
        (sizeof(header.subchunk2ID) + sizeof(header.subchunk2Size) + header.subchunk2Size);

    Q_ASSERT(*(const uint32_t*)(header.chunkID) == 0x46464952);
    Q_ASSERT(*(const uint32_t*)(header.subchunk1ID) == 0x20746d66);
    Q_ASSERT(*(const uint32_t*)(header.subchunk2ID) == 0x61746164);
    Q_ASSERT(header.chunkSize == 36 + header.subchunk2Size);

    Q_ASSERT(header.isValid());

    file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
    file.write(reinterpret_cast<const char*>(m_audio.data()), m_audio.size() * sizeof(decltype(m_audio)::value_type));

    return true;
}

#define TWO_PI (M_PI * 2.)

void AudioBuffer::generateSound(float *output, size_t frames)
{
    if (m_currentTone == Silence) {
        for (size_t i=0; i<frames; i++) {
            output[i] = 0;
        }
        return;
    }
    const int freq = frequency(m_currentTone);
    if (!freq || ! sampleRate) {
        qWarning() << "missing frequency or sample rate" << freq << sampleRate;
        return;
    }
    const double advance = double(freq) / sampleRate;
    switch(waveform) {
    case Sine:
        for (size_t i=0; i<frames; i++, m_time += advance) {
            output[i] = sin(TWO_PI * m_time) * volume;
        }
        break;
    case Sawtooth:
        for (size_t i=0; i<frames; i++, m_time += advance) {
            output[i] = 2 * ((m_time - int64_t(m_time)) - 0.5) * volume;
        }
        break;
    case Triangle:
        for (size_t i=0; i<frames; i++, m_time += advance) {
            output[i] = (2 * std::abs(2 * ((m_time - int64_t(m_time)) - 0.5)) - 1) * volume;
        }
        break;
    case Square:
        for (size_t i=0; i<frames; i++) {
            if (m_time - int64_t(m_time) < 0.5) {
                output[i] = volume;
            } else {
                output[i] = -volume;
            }
            m_time += advance;
        }
        break;
    default:
        assert(false);
        break;
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

void AudioBuffer::takeFrames(uint32_t frameCount, void *output)
{
    const size_t byteCount = sizeof(decltype(m_audio)::value_type) * frameCount;
    memset(output, '\0', byteCount); // could Optimize™ and only zero the frames at the end, but idc
    if (frameCount > m_audio.size()) {
        frameCount = m_audio.size();
    }

    memcpy(output, m_audio.data(), byteCount);
    m_audio.remove(0, frameCount);
}
