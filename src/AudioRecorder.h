#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <optional>

struct AudioRecorderBackend {
    QString id;
    QString displayName;
    QString executableName;
    QString program;
    QStringList arguments;
};

class AudioRecorder : public QObject {
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    bool start(const QString &requestedBackend, QString *errorMessage);
    bool start(QString *errorMessage);
    QByteArray stop(QString *errorMessage);
    bool isRecording() const;
    QString activeBackendName() const;

    static std::optional<AudioRecorderBackend> selectBackend(
            const QString &requestedBackend,
            QString *errorMessage = nullptr);
    static std::optional<AudioRecorderBackend> selectBackend(
            const QString &requestedBackend,
            const QStringList &searchPaths,
            QString *errorMessage);
    static bool hasSupportedBackend(const QString &requestedBackend, QString *errorMessage);

signals:
    void levelChanged(double level);
    void audioChunkCaptured(const QByteArray &chunk);
    void failed(const QString &message);

private:
    void readPendingAudio();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    static double levelFromPcm16(const QByteArray &chunk);

    QProcess process_;
    std::optional<AudioRecorderBackend> activeBackend_;
    QByteArray pcm_;
    bool stopping_ = false;
};
