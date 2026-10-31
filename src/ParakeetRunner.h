#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QTimer>

class QFile;

// Keeps one Parakeet process alive so the model loads once. The process is started
// detached (nohup), like BuzzAsrRunner. Requests and results go through two files
// polled by outputTimer_.
class ParakeetRunner : public QObject {
    Q_OBJECT

public:
    explicit ParakeetRunner(QObject *parent = nullptr);
    ~ParakeetRunner() override;

    void transcribe(const QString &wavPath, const AppSettings &settings);

signals:
    void transcriptionReady(const QString &text);
    void failed(const QString &message);

private:
    bool ensureDaemon(const AppSettings &settings, QString *errorMessage);
    bool writePendingRequest();
    void stopDaemon();
    void pollOutput();

    QString inboxPath_;
    QString outboxPath_;
    QString stderrPath_;
    qint64 daemonPid_ = 0;
    bool ready_ = false;
    QString pendingWavPath_;
    qint64 outboxOffset_ = 0;
    QTimer outputTimer_;
};
