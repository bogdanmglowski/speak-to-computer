#include "FileHandoff.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

FileHandoff::FileHandoff(QObject *parent)
    : QObject(parent)
{
}

bool FileHandoff::write(const QString &directory, const QString &text, QString *errorMessage)
{
    if (!QDir().mkpath(directory)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create handoff directory: %1").arg(directory);
        }
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
    const QString filename = directory + QStringLiteral("/handoff_%1.txt").arg(timestamp);

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not open handoff file for writing: %1").arg(filename);
        }
        return false;
    }

    QTextStream stream(&file);
    stream << text;
    if (!text.endsWith(QLatin1Char('\n'))) {
        stream << QLatin1Char('\n');
    }
    file.close();

    return true;
}
