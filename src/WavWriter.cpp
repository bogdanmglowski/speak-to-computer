#include "WavWriter.h"

#include <QDataStream>
#include <QFile>

bool WavWriter::writePcm16Mono(const QString &path, const QByteArray &pcm, int sampleRate, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not write WAV file: %1").arg(file.errorString());
        }
        return false;
    }

    constexpr quint16 channels = 1;
    constexpr quint16 bitsPerSample = 16;
    constexpr quint16 audioFormatPcm = 1;
    const quint32 dataSize = static_cast<quint32>(pcm.size());
    const quint32 byteRate = static_cast<quint32>(sampleRate * channels * bitsPerSample / 8);
    const quint16 blockAlign = channels * bitsPerSample / 8;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << static_cast<quint32>(36 + dataSize);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << static_cast<quint32>(16);
    stream << audioFormatPcm;
    stream << channels;
    stream << static_cast<quint32>(sampleRate);
    stream << byteRate;
    stream << blockAlign;
    stream << bitsPerSample;
    stream.writeRawData("data", 4);
    stream << dataSize;
    stream.writeRawData(pcm.constData(), static_cast<int>(pcm.size()));

    if (stream.status() != QDataStream::Ok) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not finish WAV file: stream write failed.");
        }
        return false;
    }

    return true;
}
