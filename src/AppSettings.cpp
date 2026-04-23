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

QString defaultWakeWordPhrase()
{
    return QStringLiteral("alexa");
}

QString appDataRootPath()
{
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::home().filePath(QStringLiteral(".local/share"));
    }
    return QDir(appDataPath).filePath(QStringLiteral("speak-to-computer"));
}

QString defaultWakeWordRuntimeDirectory()
{
    return QDir(appDataRootPath()).filePath(QStringLiteral("python"));
}

QString defaultWakeWordSidecarExecutable()
{
    return QDir(defaultWakeWordRuntimeDirectory()).filePath(QStringLiteral(".venv/bin/python"));
}

QString defaultWakeWordSidecarScript()
{
    return QDir(defaultWakeWordRuntimeDirectory()).filePath(QStringLiteral("openwakeword_sidecar.py"));
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
    if (!settings->contains(QStringLiteral("hotkey_dictate"))) {
        const QString legacyHotkey = settings->value(QStringLiteral("hotkey")).toString().trimmed();
        settings->setValue(QStringLiteral("hotkey_dictate"),
                legacyHotkey.isEmpty() ? QStringLiteral("Super+Space") : legacyHotkey);
    }
    if (!settings->contains(QStringLiteral("hotkey_translate_en"))) {
        settings->setValue(QStringLiteral("hotkey_translate_en"), QStringLiteral("Super+Shift+Space"));
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
    if (!settings->contains(QStringLiteral("wake_word_enabled"))) {
        settings->setValue(QStringLiteral("wake_word_enabled"), false);
    }
    if (!settings->contains(QStringLiteral("wake_word_phrase"))) {
        settings->setValue(QStringLiteral("wake_word_phrase"), defaultWakeWordPhrase());
    }
    if (!settings->contains(QStringLiteral("wake_word_model_path"))) {
        settings->setValue(QStringLiteral("wake_word_model_path"), QStringLiteral(""));
    }
    if (!settings->contains(QStringLiteral("wake_word_threshold"))) {
        settings->setValue(QStringLiteral("wake_word_threshold"), 0.5);
    }
    if (!settings->contains(QStringLiteral("wake_word_sidecar_executable"))) {
        settings->setValue(QStringLiteral("wake_word_sidecar_executable"), defaultWakeWordSidecarExecutable());
    }
    if (!settings->contains(QStringLiteral("wake_word_sidecar_script"))) {
        settings->setValue(QStringLiteral("wake_word_sidecar_script"), defaultWakeWordSidecarScript());
    }
    if (!settings->contains(QStringLiteral("vad_autostop_enabled"))) {
        settings->setValue(QStringLiteral("vad_autostop_enabled"), false);
    }
    if (!settings->contains(QStringLiteral("vad_aggressiveness"))) {
        settings->setValue(QStringLiteral("vad_aggressiveness"), 2);
    }
    if (!settings->contains(QStringLiteral("vad_end_silence_ms"))) {
        settings->setValue(QStringLiteral("vad_end_silence_ms"), 900);
    }
    if (!settings->contains(QStringLiteral("vad_min_speech_ms"))) {
        settings->setValue(QStringLiteral("vad_min_speech_ms"), 250);
    }

    const QString configuredSidecarExecutable =
            settings->value(QStringLiteral("wake_word_sidecar_executable")).toString().trimmed();
    const QString configuredSidecarScript =
            settings->value(QStringLiteral("wake_word_sidecar_script")).toString().trimmed();
    if (configuredSidecarExecutable == QStringLiteral("python3")
            && configuredSidecarScript == QStringLiteral("openwakeword_sidecar.py")) {
        settings->setValue(QStringLiteral("wake_word_sidecar_executable"), defaultWakeWordSidecarExecutable());
        settings->setValue(QStringLiteral("wake_word_sidecar_script"), defaultWakeWordSidecarScript());
    }

    QString normalizedSidecarExecutable = configuredSidecarExecutable;
    QString normalizedSidecarScript = configuredSidecarScript;
    normalizedSidecarExecutable.replace(
            QStringLiteral("/speak-to-computer/speak-to-computer/"),
            QStringLiteral("/speak-to-computer/"));
    normalizedSidecarScript.replace(
            QStringLiteral("/speak-to-computer/speak-to-computer/"),
            QStringLiteral("/speak-to-computer/"));
    if (normalizedSidecarExecutable != configuredSidecarExecutable
            && normalizedSidecarScript != configuredSidecarScript) {
        settings->setValue(QStringLiteral("wake_word_sidecar_executable"), normalizedSidecarExecutable);
        settings->setValue(QStringLiteral("wake_word_sidecar_script"), normalizedSidecarScript);
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

bool AppSettings::saveWakeWordEnabled(const QString &settingsPath, bool enabled, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("wake_word_enabled"), enabled);
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save wake-word settings: %1").arg(settingsPath);
    }
    return false;
}

bool AppSettings::saveVadAutostopEnabled(const QString &settingsPath, bool enabled, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("vad_autostop_enabled"), enabled);
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save VAD auto-stop setting: %1").arg(settingsPath);
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
    result.hotkeyDictate = settings.value(QStringLiteral("hotkey_dictate")).toString();
    result.hotkeyTranslateEn = settings.value(QStringLiteral("hotkey_translate_en")).toString();
    result.audioBackend = settings.value(QStringLiteral("audio_backend")).toString();
    result.language = settings.value(QStringLiteral("language")).toString();
    result.threads = settings.value(QStringLiteral("threads")).toInt();
    result.whisperCli = expandUserPath(settings.value(QStringLiteral("whisper_cli")).toString());
    result.model = expandUserPath(settings.value(QStringLiteral("model")).toString());
    result.activationSound = expandUserPath(settings.value(QStringLiteral("activation_sound")).toString());
    result.endSound = expandUserPath(settings.value(QStringLiteral("end_sound")).toString());
    result.wakeWordEnabled = settings.value(QStringLiteral("wake_word_enabled")).toBool();
    result.wakeWordPhrase = settings.value(QStringLiteral("wake_word_phrase")).toString().trimmed();
    result.wakeWordModelPath = expandUserPath(settings.value(QStringLiteral("wake_word_model_path")).toString());
    result.wakeWordThreshold = settings.value(QStringLiteral("wake_word_threshold")).toDouble();
    result.wakeWordSidecarExecutable = expandUserPath(
            settings.value(QStringLiteral("wake_word_sidecar_executable")).toString());
    result.wakeWordSidecarScript = expandUserPath(settings.value(QStringLiteral("wake_word_sidecar_script")).toString());
    result.vadAutostopEnabled = settings.value(QStringLiteral("vad_autostop_enabled")).toBool();
    result.vadAggressiveness = settings.value(QStringLiteral("vad_aggressiveness")).toInt();
    result.vadEndSilenceMs = settings.value(QStringLiteral("vad_end_silence_ms")).toInt();
    result.vadMinSpeechMs = settings.value(QStringLiteral("vad_min_speech_ms")).toInt();

    if (result.wakeWordPhrase.isEmpty()) {
        result.wakeWordPhrase = defaultWakeWordPhrase();
    }
    if (result.wakeWordThreshold <= 0.0 || result.wakeWordThreshold > 1.0) {
        result.wakeWordThreshold = 0.5;
    }
    if (result.wakeWordSidecarExecutable.isEmpty()) {
        result.wakeWordSidecarExecutable = defaultWakeWordSidecarExecutable();
    }
    if (result.wakeWordSidecarScript.isEmpty()) {
        result.wakeWordSidecarScript = defaultWakeWordSidecarScript();
    }
    if (result.vadAggressiveness < 0 || result.vadAggressiveness > 3) {
        result.vadAggressiveness = 2;
    }
    if (result.vadEndSilenceMs <= 0) {
        result.vadEndSilenceMs = 900;
    }
    if (result.vadMinSpeechMs <= 0) {
        result.vadMinSpeechMs = 250;
    }
    return result;
}

AppSettings AppSettings::load()
{
    return loadFromPath(settingsFilePath());
}
