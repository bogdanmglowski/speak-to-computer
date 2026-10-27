#pragma once

#include <QString>
#include <QStringList>

struct AppSettings {
    // whisper_cpp = batch via whisper-cli; realtime = SimulStreaming GPU sidecar;
    // buzzasr = batch via the BuzzASR/polish transformers sidecar
    static constexpr auto kEngineWhisperCpp = "whisper_cpp";
    static constexpr auto kEngineRealtime = "realtime";
    static constexpr auto kEngineBuzzAsr = "buzzasr";
    // pin = windowactivate pinned start window; focus = paste into current focus
    static constexpr auto kPasteTargetPin = "pin";
    static constexpr auto kPasteTargetFocus = "focus";

    QString settingsPath;
    QString engine; // whisper_cpp | realtime | buzzasr
    QString hotkeyDictate;
    QString hotkeyTranslateEn;
    QString audioBackend;
    QString language;
    QString whisperCli;
    QString model;
    QString realtimeModel; // e.g. small, medium (Whisper .pt name)
    QString realtimePython;
    QString realtimeServerScript;
    QString realtimeHost;
    int realtimePort = 43001;
    // Batch transcription via the BuzzASR/polish transformers sidecar.
    QString buzzAsrPython;
    QString buzzAsrScript;
    // Soft terminology bias (Whisper initial prompt / SimulStreaming static_init_prompt).
    // Empty path disables. File: one term per line, # comments.
    QString dictionaryPath;
    // Where text is inserted: pin (window at recording start) | focus (current focus).
    QString pasteTarget; // pin | focus
    bool miniOverlayEnabled = false;
    // Realtime: when this whole word is spoken at end of stream → strip it + press Enter.
    bool sendKeywordEnabled = true;
    QString sendKeyword = QStringLiteral("go");
    QString activationSound;
    QString endSound;
    bool wakeWordEnabled = false;
    QString wakeWordPhrase;
    QString wakeWordModelPath;
    double wakeWordThreshold = 0.5;
    QString wakeWordSidecarExecutable;
    QString wakeWordSidecarScript;
    bool vadAutostopEnabled = false;
    int vadAggressiveness = 2;
    int vadEndSilenceMs = 900;
    int vadMinSpeechMs = 250;
    int threads = 12;
    bool translateToEn = false;
    QString outputTarget = QStringLiteral("clipboard");
    QString handoffDirectory = QStringLiteral("/tmp/agent");
    QString handoffTriggerWords;

    bool isRealtimeEngine() const;
    bool isWhisperCppEngine() const;
    bool isBuzzAsrEngine() const;
    bool isPinPasteTarget() const;
    bool isFocusPasteTarget() const;

    static QString expandUserPath(const QString &path);
    static QString defaultDictionaryPath();
    static QString modelLabel(const QString &modelPath);
    static QString realtimeModelLabel(const QString &modelName);
    static QString engineLabel(const QString &engine);
    static QString normalizeEngine(const QString &engine);
    static QString languageLabel(const QString &language);
    static QString normalizeLanguage(const QString &language);
    static QStringList languageChoices();
    static QString pasteTargetLabel(const QString &pasteTarget);
    static QString normalizePasteTarget(const QString &pasteTarget);
    static QStringList pasteTargetChoices();
    // Build soft-prompt string from dictionary file (empty if missing/blank).
    static QString loadDictionaryPrompt(const QString &dictionaryPath);
    static QStringList existingModelPaths(const QString &currentModelPath);
    static QStringList realtimeModelChoices();
    static bool save(const AppSettings &settings, QString *errorMessage);
    static bool saveModel(const QString &settingsPath, const QString &modelPath, QString *errorMessage);
    static bool saveRealtimeModel(const QString &settingsPath, const QString &modelName, QString *errorMessage);
    static bool saveEngine(const QString &settingsPath, const QString &engine, QString *errorMessage);
    static bool saveLanguage(const QString &settingsPath, const QString &language, QString *errorMessage);
    static bool savePasteTarget(const QString &settingsPath, const QString &pasteTarget, QString *errorMessage);
    static bool saveMiniOverlayEnabled(const QString &settingsPath, bool enabled, QString *errorMessage);
    static bool saveSendKeywordEnabled(const QString &settingsPath, bool enabled, QString *errorMessage);
    static bool saveWakeWordEnabled(const QString &settingsPath, bool enabled, QString *errorMessage);
    static bool saveVadAutostopEnabled(const QString &settingsPath, bool enabled, QString *errorMessage);
    static bool saveVadEndSilenceMs(const QString &settingsPath, int endSilenceMs, QString *errorMessage);
    static AppSettings loadFromPath(const QString &settingsPath);
    static AppSettings load();
};
