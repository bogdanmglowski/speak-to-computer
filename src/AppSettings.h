#pragma once

#include <QString>
#include <QStringList>

struct AppSettings {
    QString settingsPath;
    QString hotkeyDictate;
    QString hotkeyTranslateEn;
    QString audioBackend;
    QString language;
    QString whisperCli;
    QString model;
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

    static QString expandUserPath(const QString &path);
    static QString modelLabel(const QString &modelPath);
    static QStringList existingModelPaths(const QString &currentModelPath);
    static bool saveModel(const QString &settingsPath, const QString &modelPath, QString *errorMessage);
    static bool saveWakeWordEnabled(const QString &settingsPath, bool enabled, QString *errorMessage);
    static bool saveVadAutostopEnabled(const QString &settingsPath, bool enabled, QString *errorMessage);
    static bool saveVadEndSilenceMs(const QString &settingsPath, int endSilenceMs, QString *errorMessage);
    static AppSettings loadFromPath(const QString &settingsPath);
    static AppSettings load();
};
