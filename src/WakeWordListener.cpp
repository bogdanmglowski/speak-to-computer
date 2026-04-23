#include "WakeWordListener.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

namespace {

QString sidecarScriptPath(const QString &configuredPath)
{
    if (configuredPath.isEmpty()) {
        return QString();
    }
    if (QFileInfo(configuredPath).isAbsolute()) {
        return configuredPath;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(configuredPath);
}

QString runtimeInstallScriptPath()
{
    const QString installedPath =
            QDir::home().filePath(QStringLiteral(".local/share/speak-to-computer/python/install-openwakeword-runtime.sh"));
    if (QFileInfo::exists(installedPath)) {
        return installedPath;
    }

    const QString appDirPath =
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("install-openwakeword-runtime.sh"));
    if (QFileInfo::exists(appDirPath)) {
        return appDirPath;
    }
    return installedPath;
}

QString runtimeInstallHint()
{
    return QStringLiteral("Run: %1").arg(runtimeInstallScriptPath());
}

} // namespace

WakeWordListener::WakeWordListener(QObject *parent)
    : QObject(parent)
{
    connect(&recorderProcess_, &QProcess::readyReadStandardOutput, this, &WakeWordListener::readRecorderAudio);
    connect(&recorderProcess_, &QProcess::finished, this, &WakeWordListener::handleRecorderFinished);
    connect(&recorderProcess_, &QProcess::errorOccurred, this, &WakeWordListener::handleRecorderError);

    connect(&sidecarProcess_, &QProcess::readyReadStandardOutput, this, &WakeWordListener::readSidecarOutput);
    connect(&sidecarProcess_, &QProcess::finished, this, &WakeWordListener::handleSidecarFinished);
    connect(&sidecarProcess_, &QProcess::errorOccurred, this, &WakeWordListener::handleSidecarError);
}

WakeWordListener::~WakeWordListener()
{
    stop();
}

bool WakeWordListener::start(const AppSettings &settings, QString *errorMessage)
{
    if (isRunning()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Wake-word listener is already running.");
        }
        return false;
    }
    if (settings.wakeWordPhrase.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("wake_word_phrase cannot be empty.");
        }
        return false;
    }
    if (settings.wakeWordThreshold <= 0.0 || settings.wakeWordThreshold > 1.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("wake_word_threshold must be in range (0.0, 1.0].");
        }
        return false;
    }

    stopping_ = false;
    sidecarOutputBuffer_.clear();
    if (!startSidecar(settings, errorMessage)) {
        stop();
        return false;
    }
    if (!startRecorder(settings.audioBackend, errorMessage)) {
        stop();
        return false;
    }

    detectCooldown_.invalidate();
    return true;
}

void WakeWordListener::stop()
{
    if (!isRunning()) {
        return;
    }

    stopping_ = true;
    stopProcess(&recorderProcess_, 1000, 700);
    if (sidecarProcess_.state() != QProcess::NotRunning) {
        sidecarProcess_.closeWriteChannel();
    }
    stopProcess(&sidecarProcess_, 1000, 700);
    activeBackend_.reset();
    sidecarOutputBuffer_.clear();
    stopping_ = false;
}

bool WakeWordListener::isRunning() const
{
    return recorderProcess_.state() != QProcess::NotRunning && sidecarProcess_.state() != QProcess::NotRunning;
}

void WakeWordListener::readRecorderAudio()
{
    const QByteArray chunk = recorderProcess_.readAllStandardOutput();
    if (chunk.isEmpty() || sidecarProcess_.state() == QProcess::NotRunning) {
        return;
    }

    if (sidecarProcess_.write(chunk) < 0 && !stopping_) {
        emit failed(QStringLiteral("Wake-word sidecar stdin write failed."));
    }
}

void WakeWordListener::readSidecarOutput()
{
    sidecarOutputBuffer_.append(sidecarProcess_.readAllStandardOutput());

    qsizetype lineEnd = sidecarOutputBuffer_.indexOf('\n');
    while (lineEnd >= 0) {
        const QByteArray line = sidecarOutputBuffer_.left(lineEnd).trimmed();
        sidecarOutputBuffer_.remove(0, lineEnd + 1);

        if (!line.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument json = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error == QJsonParseError::NoError && json.isObject()) {
                const QJsonObject object = json.object();
                const QString event = object.value(QStringLiteral("event")).toString();
                if (event == QStringLiteral("detected")) {
                    if (!detectCooldown_.isValid() || detectCooldown_.elapsed() >= detectCooldownMs_) {
                        detectCooldown_.restart();
                        emit detected();
                    }
                } else if (event == QStringLiteral("error")) {
                    const QString message = object.value(QStringLiteral("message")).toString();
                    emit failed(message.isEmpty() ? QStringLiteral("Wake-word sidecar reported an error.") : message);
                }
            }
        }

        lineEnd = sidecarOutputBuffer_.indexOf('\n');
    }
}

void WakeWordListener::handleRecorderFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    readRecorderAudio();
    if (stopping_) {
        return;
    }

    const QString stderrText = QString::fromUtf8(recorderProcess_.readAllStandardError()).trimmed();
    const QString details = stderrText.isEmpty() ? QStringLiteral("no details") : stderrText;
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString backendName = activeBackend_.has_value()
                ? QStringLiteral("%1 (%2)").arg(activeBackend_->id, activeBackend_->executableName)
                : QStringLiteral("audio backend");
        emit failed(QStringLiteral("Wake-word audio source %1 stopped unexpectedly: %2").arg(backendName, details));
    }
}

void WakeWordListener::handleRecorderError(QProcess::ProcessError error)
{
    if (stopping_ || error == QProcess::UnknownError) {
        return;
    }
    emit failed(QStringLiteral("Wake-word audio source error: %1").arg(recorderProcess_.errorString()));
}

void WakeWordListener::handleSidecarFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    readSidecarOutput();
    if (stopping_) {
        return;
    }

    const QString stderrText = QString::fromUtf8(sidecarProcess_.readAllStandardError()).trimmed();
    const QString details = stderrText.isEmpty() ? QStringLiteral("no details") : stderrText;
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        emit failed(QStringLiteral("Wake-word sidecar stopped unexpectedly: %1").arg(details));
    }
}

void WakeWordListener::handleSidecarError(QProcess::ProcessError error)
{
    if (stopping_ || error == QProcess::UnknownError) {
        return;
    }
    emit failed(QStringLiteral("Wake-word sidecar error: %1").arg(sidecarProcess_.errorString()));
}

bool WakeWordListener::startSidecar(const AppSettings &settings, QString *errorMessage)
{
    if (!QFileInfo(settings.wakeWordSidecarExecutable).exists()
            || !QFileInfo(settings.wakeWordSidecarExecutable).isExecutable()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Wake-word Python runtime is missing: %1. %2")
                                    .arg(settings.wakeWordSidecarExecutable, runtimeInstallHint());
        }
        return false;
    }

    const QString scriptPath = sidecarScriptPath(settings.wakeWordSidecarScript);
    if (!QFileInfo::exists(scriptPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Wake-word sidecar script not found: %1. %2")
                                    .arg(scriptPath, runtimeInstallHint());
        }
        return false;
    }

    QStringList arguments = {
            scriptPath,
            QStringLiteral("--phrase"),
            settings.wakeWordPhrase,
            QStringLiteral("--threshold"),
            QString::number(settings.wakeWordThreshold, 'f', 3),
    };
    if (!settings.wakeWordModelPath.trimmed().isEmpty()) {
        arguments << QStringLiteral("--model-path") << settings.wakeWordModelPath;
    }

    sidecarProcess_.setProgram(settings.wakeWordSidecarExecutable);
    sidecarProcess_.setArguments(arguments);
    sidecarProcess_.setProcessChannelMode(QProcess::SeparateChannels);
    sidecarProcess_.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    sidecarProcess_.start();

    if (!sidecarProcess_.waitForStarted(2000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start wake-word sidecar: %1").arg(sidecarProcess_.errorString());
        }
        return false;
    }
    return true;
}

bool WakeWordListener::startRecorder(const QString &requestedBackend, QString *errorMessage)
{
    activeBackend_ = AudioRecorder::selectBackend(requestedBackend, errorMessage);
    if (!activeBackend_.has_value()) {
        return false;
    }

    recorderProcess_.setProgram(activeBackend_->program);
    recorderProcess_.setArguments(activeBackend_->arguments);
    recorderProcess_.setProcessChannelMode(QProcess::SeparateChannels);
    recorderProcess_.start();

    if (!recorderProcess_.waitForStarted(2000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start wake-word audio source %1: %2")
                    .arg(activeBackend_->displayName, recorderProcess_.errorString());
        }
        return false;
    }

    return true;
}

void WakeWordListener::stopProcess(QProcess *process, int terminateTimeoutMs, int killTimeoutMs)
{
    if (process->state() == QProcess::NotRunning) {
        return;
    }

    process->terminate();
    if (!process->waitForFinished(terminateTimeoutMs)) {
        process->kill();
        process->waitForFinished(killTimeoutMs);
    }
}
