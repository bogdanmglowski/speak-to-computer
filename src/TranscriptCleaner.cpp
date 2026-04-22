#include "TranscriptCleaner.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

const QRegularExpression &nonSpeechAnnotationPattern()
{
    static const QRegularExpression pattern(
            QStringLiteral(R"(\s*[\[(](?:music(?: playing)?|background music|applause|laughter|laughing|silence|noise|inaudible|blank[_ ]audio|sound effect|sound|beep|cough(?:ing)?|sigh|breath(?:ing)?)[\])]\s*)"),
            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

bool isStandaloneBracketedPhrase(const QString &line)
{
    if (line.size() < 3) {
        return false;
    }

    const bool squareBracketed = line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'));
    const bool parenthesized = line.startsWith(QLatin1Char('(')) && line.endsWith(QLatin1Char(')'));
    if (!squareBracketed && !parenthesized) {
        return false;
    }

    return !line.mid(1, line.size() - 2).simplified().isEmpty();
}

} // namespace

QString TranscriptCleaner::cleanup(const QString &rawText)
{
    QString normalized = rawText;
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList cleanedLines;
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.replace(nonSpeechAnnotationPattern(), QStringLiteral(" "));
        line = line.simplified();
        if (!line.isEmpty() && !isStandaloneBracketedPhrase(line)) {
            cleanedLines << line;
        }
    }

    return cleanedLines.join(QLatin1Char(' ')).trimmed();
}
