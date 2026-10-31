#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QProcess>

class WhisperRunner : public QObject {
    Q_OBJECT

public:
    explicit WhisperRunner(QObject *parent = nullptr);
    ~WhisperRunner() override;

    void transcribe(const QString &wavPath, const AppSettings &settings, bool timestamps = false);
    bool isRunning() const { return process_.state() != QProcess::NotRunning; }

signals:
    void transcriptionReady(const QString &text);
    void failed(const QString &message);
    void progressChanged(int percent);

private:
    void consumeStandardError(bool final = false);
    QString currentWhisperCliPath_;
    QString currentModelPath_;
    QString currentWavPath_;
    QProcess process_;
    bool timestamps_ = false;
    QByteArray stderrText_;
    QByteArray stderrPending_;
    int lastProgress_ = -1;
};
