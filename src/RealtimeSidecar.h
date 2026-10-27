#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>

class RealtimeSidecar : public QObject {
    Q_OBJECT

public:
    explicit RealtimeSidecar(QObject *parent = nullptr);
    ~RealtimeSidecar() override;

    bool isServerRunning() const;
    bool isSessionActive() const;

    // Start SimulStreaming server and load model (blocks until ready or timeout).
    bool loadServer(const AppSettings &settings, QString *errorMessage);
    // Kill server process and free GPU.
    void unloadServer();

    // Hotkey session: mic → TCP → paste deltas (pin window or follow focus).
    bool startSession(quint64 targetWindow, const AppSettings &settings, QString *errorMessage);
    void stopSession();
    void setSendKeywordEnabled(bool enabled);
    void setPasteTarget(const QString &pasteTarget);
    void setTargetWindow(quint64 targetWindow);

signals:
    void serverReady();
    void serverFailed(const QString &message);
    void sessionFailed(const QString &message);
    void partialText(const QString &text);
    void transcriptionUpdated(const QString &text);
    void sessionFinished();

private slots:
    void handleMicFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleSocketReadyRead();
    void handleSocketError(QAbstractSocket::SocketError error);

private:
    QString defaultServerLogPath() const;
    QString resolveWrapperScript() const;
    // Poll TCP port; also tail log file written by run-server-for-app.sh.
    bool waitForServerReady(int timeoutMs, QString *errorMessage);
    QString drainServerLogFile();
    void killProcessesOnPort(int port) const;
    void killServerProcessTree();
    bool isServerPidAlive() const;
    bool pasteDelta(const QString &text, QString *errorMessage);
    bool sendKeys(const QStringList &keys, QString *errorMessage);
    bool sendEnter(QString *errorMessage);
    bool sendBackspaces(int count, QString *errorMessage);
    // If trailing + text ends with send keyword (whole word), strip it and request Enter.
    bool applySendKeyword(QString *text, int *backspacesOut);
    QString ensureSpacing(const QString &text);
    QString extractTextFromServerLine(const QString &line) const;
    bool startMicProcess(const QString &audioBackend, QString *errorMessage);

    // Server is launched via nohup (reparented). Direct QProcess children hang
    // CUDA init under NoMachine/KDE if the GUI thread spins nested event loops.
    qint64 serverPid_ = 0;
    QProcess micProcess_;
    QTcpSocket socket_;
    QByteArray socketBuffer_;
    QString serverLogPath_;
    qint64 serverLogReadOffset_ = 0;
    QString serverLogTail_;
    QString loadedModel_;
    QString language_;
    QString loadedDictionaryPrompt_;
    QString host_;
    int port_ = 43001;
    quint64 targetWindow_ = 0;
    // When true: windowactivate pinned target before paste/keys. When false: current focus.
    bool pinPasteTarget_ = true;
    QChar lastEmittedChar_;
    QString pasteTail_;
    QString transcription_;
    bool sendKeywordEnabled_ = true;
    QString sendKeyword_ = QStringLiteral("go");
    bool sessionActive_ = false;
    bool loading_ = false;
};
