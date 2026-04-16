#pragma once

#include <QString>

struct AppSettings {
    QString settingsPath;
    QString hotkey;
    QString audioBackend;
    QString language;
    QString whisperCli;
    QString model;
    int threads = 12;

    static QString expandUserPath(const QString &path);
    static AppSettings load();
};
