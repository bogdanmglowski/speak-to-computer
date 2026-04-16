#pragma once

#include <QByteArray>
#include <QString>

class WavWriter {
public:
    static bool writePcm16Mono(const QString &path, const QByteArray &pcm, int sampleRate, QString *errorMessage);
};
