#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

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

QString sourceOrInstallRoot()
{
#ifdef SPEAK_TO_COMPUTER_SOURCE_DIR
    const QString sourceDir = QStringLiteral(SPEAK_TO_COMPUTER_SOURCE_DIR);
    if (QDir(sourceDir).exists()) {
        return sourceDir;
    }
#endif
    return QCoreApplication::applicationDirPath();
}

QString defaultRealtimePocRoot()
{
    return QDir(sourceOrInstallRoot()).filePath(QStringLiteral("scripts/simulstreaming-poc"));
}

QString defaultRealtimePython()
{
    return QDir(defaultRealtimePocRoot()).filePath(QStringLiteral(".venv/bin/python"));
}

QString defaultRealtimeServerScript()
{
    return QDir(defaultRealtimePocRoot())
            .filePath(QStringLiteral("SimulStreaming/simulstreaming_whisper_server.py"));
}

QString defaultBuzzAsrPython()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("speak-to-computer/buzzasr/.venv/bin/python"));
}

QString defaultBuzzAsrScript()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("speak-to-computer/buzzasr/buzzasr_transcribe.py"));
}

QString settingsFilePath()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configRoot + QStringLiteral("/speak-to-computer/settings.ini");
}

QString defaultDictionaryPathValue()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configRoot + QStringLiteral("/speak-to-computer/dictionary.txt");
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
    if (!settings->contains(QStringLiteral("engine"))) {
        settings->setValue(QStringLiteral("engine"), QStringLiteral("whisper_cpp"));
    }
    if (!settings->contains(QStringLiteral("realtime_model"))) {
        settings->setValue(QStringLiteral("realtime_model"), QStringLiteral("medium"));
    }
    if (!settings->contains(QStringLiteral("realtime_python"))) {
        settings->setValue(QStringLiteral("realtime_python"), defaultRealtimePython());
    }
    if (!settings->contains(QStringLiteral("realtime_server_script"))) {
        settings->setValue(QStringLiteral("realtime_server_script"), defaultRealtimeServerScript());
    }
    if (!settings->contains(QStringLiteral("realtime_host"))) {
        settings->setValue(QStringLiteral("realtime_host"), QStringLiteral("127.0.0.1"));
    }
    if (!settings->contains(QStringLiteral("realtime_port"))) {
        settings->setValue(QStringLiteral("realtime_port"), 43001);
    }
    if (!settings->contains(QStringLiteral("buzzasr_python"))) {
        settings->setValue(QStringLiteral("buzzasr_python"), defaultBuzzAsrPython());
    }
    if (!settings->contains(QStringLiteral("buzzasr_script"))) {
        settings->setValue(QStringLiteral("buzzasr_script"), defaultBuzzAsrScript());
    }
    if (settings->value(QStringLiteral("buzzasr_python")).toString()
            == QStringLiteral("/tmp/opencode/cer-poc/.venv/bin/python")) {
        settings->setValue(QStringLiteral("buzzasr_python"), defaultBuzzAsrPython());
        if (settings->value(QStringLiteral("buzzasr_script")).toString()
                .endsWith(QStringLiteral("/scripts/buzzasr_transcribe.py"))) {
            settings->setValue(QStringLiteral("buzzasr_script"), defaultBuzzAsrScript());
        }
    }
    if (!settings->contains(QStringLiteral("dictionary_path"))) {
        settings->setValue(QStringLiteral("dictionary_path"), defaultDictionaryPathValue());
    }
    if (!settings->contains(QStringLiteral("paste_target"))) {
        settings->setValue(QStringLiteral("paste_target"), QStringLiteral("pin"));
    }
    if (!settings->contains(QStringLiteral("send_keyword_enabled"))) {
        settings->setValue(QStringLiteral("send_keyword_enabled"), true);
    }
    if (!settings->contains(QStringLiteral("send_keyword"))) {
        settings->setValue(QStringLiteral("send_keyword"), QStringLiteral("go"));
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
    if (!settings->contains(QStringLiteral("output_target"))) {
        settings->setValue(QStringLiteral("output_target"), QStringLiteral("clipboard"));
    }
    if (!settings->contains(QStringLiteral("handoff_directory"))) {
        settings->setValue(QStringLiteral("handoff_directory"), QStringLiteral("/tmp/agent"));
    }
    if (!settings->contains(QStringLiteral("handoff_trigger_words"))) {
        settings->setValue(QStringLiteral("handoff_trigger_words"), QStringLiteral(""));
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

QString AppSettings::defaultDictionaryPath()
{
    return defaultDictionaryPathValue();
}

QString AppSettings::loadDictionaryPrompt(const QString &dictionaryPath)
{
    const QString path = expandUserPath(dictionaryPath.trimmed());
    if (path.isEmpty()) {
        return {};
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QStringList terms;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        // Allow "term = wrong spelling" only as term (no replace map); take left side if '=' present? 
        // User asked prompt-only: whole non-comment line is one term/phrase.
        terms << line;
    }
    file.close();

    if (terms.isEmpty()) {
        return {};
    }

    // Soft prompt budget is small (~224 tokens). Cap roughly to keep last terms stronger.
    QString prompt = terms.join(QStringLiteral(", "));
    constexpr int kMaxPromptChars = 900;
    if (prompt.size() > kMaxPromptChars) {
        prompt = prompt.right(kMaxPromptChars);
        const int comma = prompt.indexOf(QLatin1Char(','));
        if (comma > 0 && comma < 40) {
            prompt = prompt.mid(comma + 1).trimmed();
        }
    }
    return prompt;
}

bool AppSettings::isRealtimeEngine() const
{
    return engine.trimmed().compare(QString::fromUtf8(kEngineRealtime), Qt::CaseInsensitive) == 0;
}

bool AppSettings::isWhisperCppEngine() const
{
    return !isRealtimeEngine() && !isBuzzAsrEngine();
}

bool AppSettings::isBuzzAsrEngine() const
{
    return engine.trimmed().compare(QString::fromUtf8(kEngineBuzzAsr), Qt::CaseInsensitive) == 0;
}

bool AppSettings::isPinPasteTarget() const
{
    return normalizePasteTarget(pasteTarget) == QString::fromUtf8(kPasteTargetPin);
}

bool AppSettings::isFocusPasteTarget() const
{
    return normalizePasteTarget(pasteTarget) == QString::fromUtf8(kPasteTargetFocus);
}

QString AppSettings::normalizeEngine(const QString &engine)
{
    const QString trimmed = engine.trimmed();
    if (trimmed.compare(QString::fromUtf8(kEngineRealtime), Qt::CaseInsensitive) == 0) {
        return QString::fromUtf8(kEngineRealtime);
    }
    if (trimmed.compare(QString::fromUtf8(kEngineBuzzAsr), Qt::CaseInsensitive) == 0) {
        return QString::fromUtf8(kEngineBuzzAsr);
    }
    return QString::fromUtf8(kEngineWhisperCpp);
}

QString AppSettings::engineLabel(const QString &engine)
{
    if (normalizeEngine(engine) == QString::fromUtf8(kEngineRealtime)) {
        return QStringLiteral("Live");
    }
    if (normalizeEngine(engine) == QString::fromUtf8(kEngineBuzzAsr)) {
        return QStringLiteral("BuzzASR");
    }
    return QStringLiteral("Batch");
}

QString AppSettings::normalizePasteTarget(const QString &pasteTarget)
{
    if (pasteTarget.trimmed().compare(QString::fromUtf8(kPasteTargetFocus), Qt::CaseInsensitive) == 0) {
        return QString::fromUtf8(kPasteTargetFocus);
    }
    return QString::fromUtf8(kPasteTargetPin);
}

QString AppSettings::pasteTargetLabel(const QString &pasteTarget)
{
    if (normalizePasteTarget(pasteTarget) == QString::fromUtf8(kPasteTargetFocus)) {
        return QStringLiteral("Focus");
    }
    return QStringLiteral("Pin");
}

QStringList AppSettings::pasteTargetChoices()
{
    return {
            QString::fromUtf8(kPasteTargetPin),
            QString::fromUtf8(kPasteTargetFocus),
    };
}

QString AppSettings::normalizeLanguage(const QString &language)
{
    const QString tag = language.trimmed().toLower();
    if (tag.isEmpty() || tag == QStringLiteral("auto") || tag == QStringLiteral("detect")) {
        return QStringLiteral("auto");
    }
    if (tag.startsWith(QStringLiteral("en"))) {
        return QStringLiteral("en");
    }
    if (tag.startsWith(QStringLiteral("pl"))) {
        return QStringLiteral("pl");
    }
    // Keep short codes as-is (whisper accepts ISO codes).
    const int dash = tag.indexOf(QLatin1Char('-'));
    const int under = tag.indexOf(QLatin1Char('_'));
    int cut = tag.size();
    if (dash > 0) {
        cut = dash;
    }
    if (under > 0 && under < cut) {
        cut = under;
    }
    return tag.left(cut);
}

QString AppSettings::languageLabel(const QString &language)
{
    return normalizeLanguage(language);
}

QStringList AppSettings::languageChoices()
{
    return {
            QStringLiteral("pl"),
            QStringLiteral("en"),
            QStringLiteral("auto"),
    };
}

QString AppSettings::modelLabel(const QString &modelPath)
{
    if (modelPath.startsWith(QStringLiteral("realtime:"))) {
        return modelPath.mid(QStringLiteral("realtime:").size());
    }
    const QString modelName = QFileInfo(modelPath).fileName();
    if (modelName.startsWith(QStringLiteral("ggml-")) && modelName.endsWith(QStringLiteral(".bin"))) {
        return modelName.mid(5, modelName.size() - 9);
    }
    if (!modelName.isEmpty()) {
        return modelName;
    }
    return QStringLiteral("Custom");
}

QString AppSettings::realtimeModelLabel(const QString &modelName)
{
    const QString name = modelName.trimmed();
    return name.isEmpty() ? QStringLiteral("medium") : name;
}

QStringList AppSettings::realtimeModelChoices()
{
    return {
            QStringLiteral("tiny"),
            QStringLiteral("base"),
            QStringLiteral("small"),
            QStringLiteral("medium"),
            QStringLiteral("large-v2"),
            QStringLiteral("large-v3"),
            QStringLiteral("large-v3-turbo"),
    };
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

bool AppSettings::save(const AppSettings &settingsData, QString *errorMessage)
{
    if (settingsData.settingsPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Settings path is empty.");
        }
        return false;
    }
    if (settingsData.wakeWordThreshold <= 0.0 || settingsData.wakeWordThreshold > 1.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Wake-word threshold must be between 0 and 1.");
        }
        return false;
    }
    if (settingsData.vadAggressiveness < 0 || settingsData.vadAggressiveness > 3) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VAD aggressiveness must be between 0 and 3.");
        }
        return false;
    }
    if (settingsData.vadEndSilenceMs <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VAD auto-stop silence must be positive.");
        }
        return false;
    }
    if (settingsData.vadMinSpeechMs <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VAD minimum speech must be positive.");
        }
        return false;
    }
    if (settingsData.threads <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Thread count must be positive.");
        }
        return false;
    }

    QDir().mkpath(QFileInfo(settingsData.settingsPath).absolutePath());
    QSettings settings(settingsData.settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("hotkey_dictate"), settingsData.hotkeyDictate.trimmed());
    settings.setValue(QStringLiteral("hotkey_translate_en"), settingsData.hotkeyTranslateEn.trimmed());
    settings.setValue(QStringLiteral("audio_backend"), settingsData.audioBackend.trimmed());
    settings.setValue(QStringLiteral("language"), settingsData.language.trimmed());
    settings.setValue(QStringLiteral("threads"), settingsData.threads);
    settings.setValue(QStringLiteral("engine"), settingsData.engine.trimmed().isEmpty()
                    ? QStringLiteral("whisper_cpp")
                    : settingsData.engine.trimmed());
    settings.setValue(QStringLiteral("whisper_cli"), settingsData.whisperCli.trimmed());
    settings.setValue(QStringLiteral("model"), settingsData.model.trimmed());
    settings.setValue(QStringLiteral("realtime_model"), settingsData.realtimeModel.trimmed());
    settings.setValue(QStringLiteral("realtime_python"), settingsData.realtimePython.trimmed());
    settings.setValue(QStringLiteral("realtime_server_script"), settingsData.realtimeServerScript.trimmed());
    settings.setValue(QStringLiteral("realtime_host"), settingsData.realtimeHost.trimmed());
    settings.setValue(QStringLiteral("realtime_port"), settingsData.realtimePort);
    settings.setValue(QStringLiteral("buzzasr_python"), settingsData.buzzAsrPython.trimmed());
    settings.setValue(QStringLiteral("buzzasr_script"), settingsData.buzzAsrScript.trimmed());
    settings.setValue(QStringLiteral("dictionary_path"), settingsData.dictionaryPath.trimmed());
    settings.setValue(QStringLiteral("paste_target"), normalizePasteTarget(settingsData.pasteTarget));
    settings.setValue(QStringLiteral("mini_overlay_enabled"), settingsData.miniOverlayEnabled);
    settings.setValue(QStringLiteral("send_keyword_enabled"), settingsData.sendKeywordEnabled);
    settings.setValue(QStringLiteral("send_keyword"), settingsData.sendKeyword.trimmed());
    settings.setValue(QStringLiteral("activation_sound"), settingsData.activationSound.trimmed());
    settings.setValue(QStringLiteral("end_sound"), settingsData.endSound.trimmed());
    settings.setValue(QStringLiteral("wake_word_enabled"), settingsData.wakeWordEnabled);
    settings.setValue(QStringLiteral("wake_word_phrase"), settingsData.wakeWordPhrase.trimmed());
    settings.setValue(QStringLiteral("wake_word_model_path"), settingsData.wakeWordModelPath.trimmed());
    settings.setValue(QStringLiteral("wake_word_threshold"), settingsData.wakeWordThreshold);
    settings.setValue(QStringLiteral("wake_word_sidecar_executable"), settingsData.wakeWordSidecarExecutable.trimmed());
    settings.setValue(QStringLiteral("wake_word_sidecar_script"), settingsData.wakeWordSidecarScript.trimmed());
    settings.setValue(QStringLiteral("vad_autostop_enabled"), settingsData.vadAutostopEnabled);
    settings.setValue(QStringLiteral("vad_aggressiveness"), settingsData.vadAggressiveness);
    settings.setValue(QStringLiteral("vad_end_silence_ms"), settingsData.vadEndSilenceMs);
    settings.setValue(QStringLiteral("vad_min_speech_ms"), settingsData.vadMinSpeechMs);
    settings.setValue(QStringLiteral("output_target"), settingsData.outputTarget.trimmed());
    settings.setValue(QStringLiteral("handoff_directory"), settingsData.handoffDirectory.trimmed());
    settings.setValue(QStringLiteral("handoff_trigger_words"), settingsData.handoffTriggerWords.trimmed());
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save settings: %1").arg(settingsData.settingsPath);
    }
    return false;
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

bool AppSettings::saveRealtimeModel(const QString &settingsPath, const QString &modelName, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("realtime_model"), modelName.trimmed());
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save realtime model in settings: %1").arg(settingsPath);
    }
    return false;
}

bool AppSettings::saveEngine(const QString &settingsPath, const QString &engine, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("engine"), normalizeEngine(engine));
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save engine in settings: %1").arg(settingsPath);
    }
    return false;
}

bool AppSettings::saveLanguage(const QString &settingsPath, const QString &language, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("language"), normalizeLanguage(language));
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save language in settings: %1").arg(settingsPath);
    }
    return false;
}

bool AppSettings::savePasteTarget(const QString &settingsPath, const QString &pasteTarget, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("paste_target"), normalizePasteTarget(pasteTarget));
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save paste target in settings: %1").arg(settingsPath);
    }
    return false;
}

bool AppSettings::saveSendKeywordEnabled(const QString &settingsPath, bool enabled, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("send_keyword_enabled"), enabled);
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save send keyword toggle in settings: %1").arg(settingsPath);
    }
    return false;
}

bool AppSettings::saveMiniOverlayEnabled(const QString &settingsPath, bool enabled, QString *errorMessage)
{
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("mini_overlay_enabled"), enabled);
    settings.sync();
    if (settings.status() == QSettings::NoError) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save mini overlay setting: %1").arg(settingsPath);
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

bool AppSettings::saveVadEndSilenceMs(const QString &settingsPath, int endSilenceMs, QString *errorMessage)
{
    if (endSilenceMs <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VAD auto-stop silence must be positive.");
        }
        return false;
    }

    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("vad_end_silence_ms"), endSilenceMs);
    settings.sync();

    if (settings.status() == QSettings::NoError) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Failed to save VAD silence duration: %1").arg(settingsPath);
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
    result.engine = normalizeEngine(settings.value(QStringLiteral("engine")).toString());
    result.audioBackend = settings.value(QStringLiteral("audio_backend")).toString();
    result.language = settings.value(QStringLiteral("language")).toString();
    result.threads = settings.value(QStringLiteral("threads")).toInt();
    result.whisperCli = expandUserPath(settings.value(QStringLiteral("whisper_cli")).toString());
    result.model = expandUserPath(settings.value(QStringLiteral("model")).toString());
    result.realtimeModel = settings.value(QStringLiteral("realtime_model"), QStringLiteral("medium")).toString().trimmed();
    if (result.realtimeModel.isEmpty()) {
        result.realtimeModel = QStringLiteral("medium");
    }
    result.realtimePython = expandUserPath(settings.value(QStringLiteral("realtime_python")).toString());
    result.realtimeServerScript = expandUserPath(settings.value(QStringLiteral("realtime_server_script")).toString());
    result.realtimeHost = settings.value(QStringLiteral("realtime_host"), QStringLiteral("127.0.0.1")).toString().trimmed();
    if (result.realtimeHost.isEmpty()) {
        result.realtimeHost = QStringLiteral("127.0.0.1");
    }
    result.buzzAsrPython = expandUserPath(settings.value(QStringLiteral("buzzasr_python")).toString());
    result.buzzAsrScript = expandUserPath(settings.value(QStringLiteral("buzzasr_script")).toString());
    result.realtimePort = settings.value(QStringLiteral("realtime_port"), 43001).toInt();
    if (result.realtimePort <= 0 || result.realtimePort > 65535) {
        result.realtimePort = 43001;
    }
    result.dictionaryPath = expandUserPath(
            settings.value(QStringLiteral("dictionary_path"), defaultDictionaryPathValue()).toString());
    if (result.dictionaryPath.trimmed().isEmpty()) {
        result.dictionaryPath = defaultDictionaryPathValue();
    }
    result.pasteTarget = normalizePasteTarget(
            settings.value(QStringLiteral("paste_target"), QStringLiteral("pin")).toString());
    result.miniOverlayEnabled = settings.value(QStringLiteral("mini_overlay_enabled"), false).toBool();
    result.sendKeywordEnabled = settings.value(QStringLiteral("send_keyword_enabled"), true).toBool();
    result.sendKeyword = settings.value(QStringLiteral("send_keyword"), QStringLiteral("go")).toString().trimmed();
    if (result.sendKeyword.isEmpty()) {
        result.sendKeyword = QStringLiteral("go");
    }
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
    result.outputTarget = settings.value(QStringLiteral("output_target")).toString().trimmed();
    result.handoffDirectory = settings.value(QStringLiteral("handoff_directory")).toString().trimmed();
    result.handoffTriggerWords = settings.value(QStringLiteral("handoff_trigger_words")).toString().trimmed();

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
