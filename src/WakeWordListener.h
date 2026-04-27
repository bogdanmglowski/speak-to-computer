#pragma once

#include "AppSettings.h"
#include "AudioRecorder.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>

class WakeWordListener : public QObject {
    Q_OBJECT

public:
    explicit WakeWordListener(QObject *parent = nullptr);
    ~WakeWordListener() override;

    bool start(const AppSettings &settings, QString *errorMessage);
    void stop();
    bool isRunning() const;

signals:
    void detected();
    void failed(const QString &message);

private:
    void readRecorderAudio();
    void readSidecarOutput();
    void handleRecorderFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleRecorderError(QProcess::ProcessError error);
    void handleSidecarFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleSidecarError(QProcess::ProcessError error);
    bool startSidecar(const AppSettings &settings, QString *errorMessage);
    bool startRecorder(const QString &requestedBackend, QString *errorMessage);
    void stopProcess(QProcess *process, int terminateTimeoutMs, int killTimeoutMs);

    QProcess recorderProcess_;
    QProcess sidecarProcess_;
    std::optional<AudioRecorderBackend> activeBackend_;
    QByteArray sidecarOutputBuffer_;
    bool stopping_ = false;
    QElapsedTimer detectCooldown_;
    int detectCooldownMs_ = 1500;
};
