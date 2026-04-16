#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QProcess>

class WhisperRunner : public QObject {
    Q_OBJECT

public:
    explicit WhisperRunner(QObject *parent = nullptr);

    void transcribe(const QString &wavPath, const AppSettings &settings);

signals:
    void transcriptionReady(const QString &text);
    void failed(const QString &message);

private:
    QProcess process_;
};
