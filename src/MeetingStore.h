#pragma once

#include <QDateTime>
#include <QString>
#include <QList>

struct MeetingEntry {
    QString directory;
    QString name;
    QDateTime started;
    qint64 durationSeconds = 0;
    QString status;
};

class MeetingStore {
public:
    static QList<MeetingEntry> list(const QString &root);
    static bool create(const QString &root, MeetingEntry *entry, QString *error);
    static bool save(const MeetingEntry &entry, QString *error);
    static QString audioPath(const MeetingEntry &entry);
    static QString transcriptPath(const MeetingEntry &entry);
};
