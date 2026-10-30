#include "MeetingStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

QList<MeetingEntry> MeetingStore::list(const QString &root)
{
    QList<MeetingEntry> result;
    const QDir dir(root);
    for (const QFileInfo &child : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::Reversed)) {
        QFile file(QDir(child.absoluteFilePath()).filePath(QStringLiteral("meeting.json")));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();
        MeetingEntry entry{child.absoluteFilePath(), obj.value(QStringLiteral("name")).toString(),
                QDateTime::fromString(obj.value(QStringLiteral("started")).toString(), Qt::ISODateWithMs),
                obj.value(QStringLiteral("durationSeconds")).toVariant().toLongLong(),
                obj.value(QStringLiteral("status")).toString()};
        if (!entry.started.isValid() || entry.name.isEmpty()) continue;
        result.append(entry);
    }
    return result;
}

bool MeetingStore::create(const QString &root, MeetingEntry *entry, QString *error)
{
    if (root.trimmed().isEmpty() || !QDir().mkpath(root)) {
        if (error) *error = QStringLiteral("Cannot create meeting recordings directory: %1").arg(root);
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QString id = now.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")) + QLatin1Char('-')
            + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    entry->directory = QDir(root).absoluteFilePath(id);
    entry->name = now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    entry->started = now;
    entry->durationSeconds = 0;
    entry->status = QStringLiteral("Recording");
    if (!QDir().mkdir(entry->directory)) {
        if (error) *error = QStringLiteral("Cannot create meeting directory: %1").arg(entry->directory);
        return false;
    }
    if (!save(*entry, error)) {
        QDir().rmdir(entry->directory);
        return false;
    }
    return true;
}

bool MeetingStore::save(const MeetingEntry &entry, QString *error)
{
    QSaveFile file(QDir(entry.directory).filePath(QStringLiteral("meeting.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Cannot write meeting metadata: %1").arg(file.errorString());
        return false;
    }
    QJsonObject obj{{QStringLiteral("name"), entry.name},
            {QStringLiteral("started"), entry.started.toString(Qt::ISODateWithMs)},
            {QStringLiteral("durationSeconds"), static_cast<double>(entry.durationSeconds)},
            {QStringLiteral("status"), entry.status}};
    const QByteArray data = QJsonDocument(obj).toJson();
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Cannot save meeting metadata: %1").arg(file.errorString());
        return false;
    }
    return true;
}

QString MeetingStore::audioPath(const MeetingEntry &entry)
{
    return QDir(entry.directory).filePath(QStringLiteral("audio.flac"));
}

QString MeetingStore::transcriptPath(const MeetingEntry &entry)
{
    return QDir(entry.directory).filePath(QStringLiteral("transcript.txt"));
}
