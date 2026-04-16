#include "TranscriptCleaner.h"

#include <QStringList>

QString TranscriptCleaner::cleanup(const QString &rawText)
{
    QString normalized = rawText;
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList cleanedLines;
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.simplified();
        if (!line.isEmpty()) {
            cleanedLines << line;
        }
    }

    return cleanedLines.join(QLatin1Char(' ')).trimmed();
}
