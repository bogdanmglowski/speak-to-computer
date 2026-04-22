#include "AppSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString defaultWhisperCli()
{
    return QDir::home().filePath(QStringLiteral("whisper.cpp/build/bin/whisper-cli"));
}

QString defaultModel()
{
    return QDir::home().filePath(QStringLiteral("whisper.cpp/models/ggml-small.bin"));
}

QString bundledActivationSound()
{
    return QStringLiteral("activation_sound.wav");
}

QString bundledEndSound()
{
    return QStringLiteral("end_sound.wav");
}

QString settingsFilePath()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configRoot + QStringLiteral("/speak-to-computer/settings.ini");
}

QString modelDirectoryPathFor(const QString &currentModelPath)
{
    QFileInfo currentModelInfo(currentModelPath);
    QString modelDirectory = currentModelInfo.absolutePath();
    if (modelDirectory.isEmpty() || modelDirectory == QStringLiteral(".")) {
        modelDirectory = QFileInfo(defaultModel()).absolutePath();
    }
    return modelDirectory;
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
    if (!settings->contains(QStringLiteral("activation_sound"))) {
        settings->setValue(QStringLiteral("activation_sound"), bundledActivationSound());
    }
    if (!settings->contains(QStringLiteral("end_sound"))) {
        settings->setValue(QStringLiteral("end_sound"), bundledEndSound());
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

QString AppSettings::modelLabel(const QString &modelPath)
{
    const QString modelName = QFileInfo(modelPath).fileName();
    if (modelName.startsWith(QStringLiteral("ggml-")) && modelName.endsWith(QStringLiteral(".bin"))) {
        return modelName.mid(5, modelName.size() - 9);
    }
    if (!modelName.isEmpty()) {
        return modelName;
    }
    return QStringLiteral("Custom");
}

QStringList AppSettings::existingModelPaths(const QString &currentModelPath)
{
    const QDir modelsDir(modelDirectoryPathFor(currentModelPath));
    const QFileInfoList entries = modelsDir.entryInfoList(
            {QStringLiteral("ggml-*.bin")},
            QDir::Files | QDir::Readable,
            QDir::NoSort);

    QFileInfoList sortedEntries = entries;
    std::sort(sortedEntries.begin(), sortedEntries.end(), [](const QFileInfo &left, const QFileInfo &right) {
        if (left.size() != right.size()) {
            return left.size() < right.size();
        }
        return left.fileName().compare(right.fileName(), Qt::CaseInsensitive) < 0;
    });

    QStringList modelPaths;
    for (const QFileInfo &entry : sortedEntries) {
        if (entry.size() > 0) {
            modelPaths << entry.absoluteFilePath();
        }
    }
    return modelPaths;
}

bool AppSettings::saveModel(const QString &settingsPath, const QString &modelPath, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("model"), modelPath);
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save model in settings: %1").arg(settingsPath);
    }
    return false;
}

AppSettings AppSettings::loadFromPath(const QString &settingsPath)
{
    QDir().mkpath(QFileInfo(settingsPath).absolutePath());

    QSettings settings(settingsPath, QSettings::IniFormat);
    ensureDefaults(&settings);
    settings.sync();

    AppSettings result;
    result.settingsPath = settingsPath;
    result.hotkey = settings.value(QStringLiteral("hotkey")).toString();
    result.audioBackend = settings.value(QStringLiteral("audio_backend")).toString();
    result.language = settings.value(QStringLiteral("language")).toString();
    result.threads = settings.value(QStringLiteral("threads")).toInt();
    result.translateToEn = readBooleanSetting(settings, QStringLiteral("translate-to-en"));
    result.whisperCli = expandUserPath(settings.value(QStringLiteral("whisper_cli")).toString());
    result.model = expandUserPath(settings.value(QStringLiteral("model")).toString());
    result.activationSound = expandUserPath(settings.value(QStringLiteral("activation_sound")).toString());
    result.endSound = expandUserPath(settings.value(QStringLiteral("end_sound")).toString());
    return result;
}

AppSettings AppSettings::load()
{
    return loadFromPath(settingsFilePath());
}
