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
    int threads = 12;
    bool translateToEn = false;

    static QString expandUserPath(const QString &path);
    static QString modelLabel(const QString &modelPath);
    static QStringList existingModelPaths(const QString &currentModelPath);
    static bool saveModel(const QString &settingsPath, const QString &modelPath, QString *errorMessage);
    static AppSettings loadFromPath(const QString &settingsPath);
    static AppSettings load();
};
