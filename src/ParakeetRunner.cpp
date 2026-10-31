#include "ParakeetRunner.h"

#include "TranscriptCleaner.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#include <signal.h>

namespace {

bool pidIsAlive(qint64 pid)
{
    return pid > 0 && kill(static_cast<pid_t>(pid), 0) == 0;
}

} // namespace

ParakeetRunner::ParakeetRunner(QObject *parent)
    : QObject(parent)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString basePath = QDir(runtimeDir).filePath(
            QStringLiteral("speak-to-computer-parakeet-%1").arg(QCoreApplication::applicationPid()));
    inboxPath_ = basePath + QStringLiteral(".in");
    outboxPath_ = basePath + QStringLiteral(".out");
    stderrPath_ = basePath + QStringLiteral(".err");
    connect(&outputTimer_, &QTimer::timeout, this, &ParakeetRunner::pollOutput);
    outputTimer_.setInterval(200);
}

ParakeetRunner::~ParakeetRunner()
{
    stopDaemon();
    QFile::remove(inboxPath_);
    QFile::remove(outboxPath_);
    QFile::remove(stderrPath_);
}

bool ParakeetRunner::ensureDaemon(const AppSettings &settings, QString *errorMessage)
{
    if (pidIsAlive(daemonPid_)) {
        return true;
    }

    QFile::remove(inboxPath_);
    QFile::remove(outboxPath_);
    QFile::remove(stderrPath_);
    outboxOffset_ = 0;
    ready_ = false;

    const QString shellCmd = QStringLiteral("INBOX=\"$1\" nohup \"$4\" -u \"$5\" >\"$2\" 2>\"$3\" & echo $!");
    QProcess launcher;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList preloadLibraries = environment.value(QStringLiteral("LD_PRELOAD"))
            .split(QRegularExpression(QStringLiteral("[:\\s]+")), Qt::SkipEmptyParts);
    preloadLibraries.removeAll(QStringLiteral("/usr/NX/lib/libnxegl.so"));
    if (preloadLibraries.isEmpty()) {
        environment.remove(QStringLiteral("LD_PRELOAD"));
    } else {
        environment.insert(QStringLiteral("LD_PRELOAD"), preloadLibraries.join(QLatin1Char(':')));
    }
    launcher.setProcessEnvironment(environment);
    launcher.start(QStringLiteral("bash"), {QStringLiteral("-c"), shellCmd, QStringLiteral("parakeet"),
            inboxPath_, outboxPath_, stderrPath_, settings.parakeetPython, settings.parakeetScript});
    if (!launcher.waitForFinished(5000) || launcher.exitCode() != 0) {
        *errorMessage = QStringLiteral("Parakeet launcher failed: %1").arg(QString::fromUtf8(launcher.readAllStandardError()));
        return false;
    }
    bool pidOk = false;
    daemonPid_ = QString::fromUtf8(launcher.readAllStandardOutput()).trimmed().toLongLong(&pidOk);
    if (!pidOk || daemonPid_ <= 0) {
        daemonPid_ = 0;
        *errorMessage = QStringLiteral("Parakeet launcher did not return a process ID.");
        return false;
    }
    outputTimer_.start();
    qInfo().noquote() << "parakeet daemon started pid" << daemonPid_;
    return true;
}

void ParakeetRunner::stopDaemon()
{
    outputTimer_.stop();
    ready_ = false;
    if (pidIsAlive(daemonPid_)) {
        kill(static_cast<pid_t>(daemonPid_), SIGKILL);
    }
    daemonPid_ = 0;
}

void ParakeetRunner::transcribe(const QString &wavPath, const AppSettings &settings)
{
    if (!pendingWavPath_.isEmpty()) {
        emit failed(QStringLiteral("Parakeet is already transcribing."));
        return;
    }
    QString errorMessage;
    if (!ensureDaemon(settings, &errorMessage)) {
        emit failed(errorMessage);
        return;
    }
    pendingWavPath_ = wavPath;
    if (!ready_) {
        return;
    }
    writePendingRequest();
}

bool ParakeetRunner::writePendingRequest()
{
    QFile inbox(inboxPath_);
    if (!inbox.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        pendingWavPath_.clear();
        emit failed(QStringLiteral("Parakeet failed: cannot write request."));
        return false;
    }
    const QJsonObject request{{QStringLiteral("wav_path"), pendingWavPath_}};
    const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (inbox.write(line) != line.size() || !inbox.flush()) {
        pendingWavPath_.clear();
        emit failed(QStringLiteral("Parakeet failed: cannot write request."));
        return false;
    }
    return true;
}

void ParakeetRunner::pollOutput()
{
    QFile outbox(outboxPath_);
    if (outbox.open(QIODevice::ReadOnly)) {
        outbox.seek(outboxOffset_);
        while (true) {
            const QByteArray raw = outbox.readLine();
            if (raw.isEmpty() || !raw.endsWith('\n')) {
                break;
            }
            outboxOffset_ = outbox.pos();
            const QJsonObject response = QJsonDocument::fromJson(raw).object();
            if (response.value(QStringLiteral("ready")).toBool()) {
                ready_ = true;
                if (!pendingWavPath_.isEmpty()) {
                    writePendingRequest();
                }
                continue;
            }
            if (pendingWavPath_.isEmpty()) {
                continue;
            }
            pendingWavPath_.clear();
            if (response.contains(QStringLiteral("error"))) {
                emit failed(QStringLiteral("Parakeet: %1").arg(response.value(QStringLiteral("error")).toString()));
                continue;
            }
            emit transcriptionReady(TranscriptCleaner::cleanup(response.value(QStringLiteral("text")).toString()));
        }
    }

    if (!pidIsAlive(daemonPid_)) {
        outputTimer_.stop();
        daemonPid_ = 0;
        ready_ = false;
        if (!pendingWavPath_.isEmpty()) {
            pendingWavPath_.clear();
            QFile stderrFile(stderrPath_);
            QString detail;
            if (stderrFile.open(QIODevice::ReadOnly)) {
                stderrFile.seek(qMax<qint64>(0, stderrFile.size() - 8192));
                detail = QString::fromUtf8(stderrFile.readAll()).trimmed();
            }
            emit failed(QStringLiteral("Parakeet stopped unexpectedly. %1").arg(detail));
        }
    }
}
