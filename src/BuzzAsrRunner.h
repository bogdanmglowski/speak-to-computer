#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QTimer>

class QFile;

// Keeps one BuzzASR process alive so the model loads once. The process is started
// detached (nohup): a direct Qt child hangs in torch/CUDA init in this session.
// Requests and results go through two files polled by outputTimer_.
class BuzzAsrRunner : public QObject {
    Q_OBJECT

public:
    explicit BuzzAsrRunner(QObject *parent = nullptr);
    ~BuzzAsrRunner() override;

    void transcribe(const QString &wavPath, const AppSettings &settings);

    // Previous runs can leave a model process holding GPU memory. Kill those by script name.
    static void killOrphanedDaemons();

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
