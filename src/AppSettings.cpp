#include "AppSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString defaultWhisperCli()
{
    return QDir::home().filePath(QStringLiteral("whisper.cpp/build/bin/whisper-cli"));
}

QString defaultModel()
{
    return QDir::home().filePath(QStringLiteral("whisper.cpp/models/ggml-small.bin"));
}

QString settingsFilePath()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configRoot + QStringLiteral("/speak-to-computer/settings.ini");
}

void ensureDefaults(QSettings *settings)
{
    if (!settings->contains(QStringLiteral("hotkey"))) {
        settings->setValue(QStringLiteral("hotkey"), QStringLiteral("Super+Space"));
    }
    if (!settings->contains(QStringLiteral("language"))) {
        settings->setValue(QStringLiteral("language"), QStringLiteral("pl"));
    }
    if (!settings->contains(QStringLiteral("audio_backend"))) {
        settings->setValue(QStringLiteral("audio_backend"), QStringLiteral("auto"));
    }
    if (!settings->contains(QStringLiteral("threads"))) {
        settings->setValue(QStringLiteral("threads"), 12);
    }
    if (!settings->contains(QStringLiteral("whisper_cli"))) {
        settings->setValue(QStringLiteral("whisper_cli"), defaultWhisperCli());
    }
    if (!settings->contains(QStringLiteral("model"))) {
        settings->setValue(QStringLiteral("model"), defaultModel());
    }
}

} // namespace

QString AppSettings::expandUserPath(const QString &path)
{
    if (path == QStringLiteral("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QStringLiteral("~/"))) {
        return QDir::home().filePath(path.mid(2));
    }
    return path;
}

AppSettings AppSettings::load()
{
    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSettings settings(path, QSettings::IniFormat);
    ensureDefaults(&settings);
    settings.sync();

    AppSettings result;
    result.settingsPath = path;
    result.hotkey = settings.value(QStringLiteral("hotkey")).toString();
    result.audioBackend = settings.value(QStringLiteral("audio_backend")).toString();
    result.language = settings.value(QStringLiteral("language")).toString();
    result.threads = settings.value(QStringLiteral("threads")).toInt();
    result.whisperCli = expandUserPath(settings.value(QStringLiteral("whisper_cli")).toString());
    result.model = expandUserPath(settings.value(QStringLiteral("model")).toString());
    return result;
}
