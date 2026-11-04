#include "BuzzAsrRunner.h"

#include "TranscriptCleaner.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QThread>

#include <signal.h>

namespace {

bool pidIsAlive(qint64 pid)
{
    return pid > 0 && kill(static_cast<pid_t>(pid), 0) == 0;
}

QList<qint64> buzzAsrDaemonPids()
{
    QProcess pgrep;
    pgrep.start(QStringLiteral("pgrep"), {QStringLiteral("-f"), QStringLiteral("buzzasr_transcribe.py")});
    if (!pgrep.waitForFinished(1000)) {
        return {};
    }
    QList<qint64> pids;
    const QStringList lines = QString::fromUtf8(pgrep.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        bool ok = false;
        const qint64 pid = line.trimmed().toLongLong(&ok);
        if (ok && pid > 0 && pid != pgrep.processId()) {
            pids << pid;
        }
    }
    return pids;
}

} // namespace

BuzzAsrRunner::BuzzAsrRunner(QObject *parent)
    : QObject(parent)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    inboxPath_ = QDir(runtimeDir).filePath(QStringLiteral("speak-to-computer-buzzasr.in"));
    outboxPath_ = QDir(runtimeDir).filePath(QStringLiteral("speak-to-computer-buzzasr.out"));
    stderrPath_ = QDir(runtimeDir).filePath(QStringLiteral("speak-to-computer-buzzasr.err"));
    connect(&outputTimer_, &QTimer::timeout, this, &BuzzAsrRunner::pollOutput);
    outputTimer_.setInterval(200);
}

BuzzAsrRunner::~BuzzAsrRunner()
{
    stopDaemon();
    killOrphanedDaemons();
}

void BuzzAsrRunner::killOrphanedDaemons()
{
    const QList<qint64> pids = buzzAsrDaemonPids();
    if (pids.isEmpty()) {
        return;
    }
    qInfo().noquote() << "stopping orphaned buzzasr daemons" << pids;
    for (qint64 pid : pids) {
        kill(static_cast<pid_t>(pid), SIGKILL);
    }
    for (int attempt = 0; attempt < 20 && !buzzAsrDaemonPids().isEmpty(); ++attempt) {
        QThread::msleep(100);
    }
}

bool BuzzAsrRunner::ensureDaemon(const AppSettings &settings, QString *errorMessage)
{
    if (pidIsAlive(daemonPid_)) {
        return true;
    }

    killOrphanedDaemons();
    QFile::remove(inboxPath_);
    QFile::remove(outboxPath_);
    QFile::remove(stderrPath_);
    outboxOffset_ = 0;
    ready_ = false;

    const QString shellCmd = QStringLiteral("INBOX=\"$1\" nohup \"$4\" -u \"$5\" >\"$2\" 2>\"$3\" & echo $!");
    QProcess launcher;
    launcher.start(QStringLiteral("bash"), {QStringLiteral("-c"), shellCmd, QStringLiteral("buzzasr"),
            inboxPath_, outboxPath_, stderrPath_, settings.buzzAsrPython, settings.buzzAsrScript});
    if (!launcher.waitForFinished(5000) || launcher.exitCode() != 0) {
        *errorMessage = QStringLiteral("BuzzASR launcher failed: %1").arg(QString::fromUtf8(launcher.readAllStandardError()));
        return false;
    }
    bool pidOk = false;
    daemonPid_ = QString::fromUtf8(launcher.readAllStandardOutput()).trimmed().toLongLong(&pidOk);
    if (!pidOk || daemonPid_ <= 0) {
        daemonPid_ = 0;
        *errorMessage = QStringLiteral("BuzzASR launcher did not return a process ID.");
        return false;
    }
    outputTimer_.start();
    qInfo().noquote() << "buzzasr daemon started pid" << daemonPid_;
    return true;
}

void BuzzAsrRunner::stopDaemon()
{
    outputTimer_.stop();
    ready_ = false;
    if (pidIsAlive(daemonPid_)) {
        kill(static_cast<pid_t>(daemonPid_), SIGKILL);
    }
    daemonPid_ = 0;
}

void BuzzAsrRunner::transcribe(const QString &wavPath, const AppSettings &settings)
{
    if (!pendingWavPath_.isEmpty()) {
        emit failed(QStringLiteral("BuzzASR is already transcribing."));
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

bool BuzzAsrRunner::writePendingRequest()
{
    QFile inbox(inboxPath_);
    if (!inbox.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        pendingWavPath_.clear();
        emit failed(QStringLiteral("BuzzASR failed: cannot write request."));
        return false;
    }
    const QJsonObject request{{QStringLiteral("wav_path"), pendingWavPath_}};
    const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (inbox.write(line) != line.size() || !inbox.flush()) {
        pendingWavPath_.clear();
        emit failed(QStringLiteral("BuzzASR failed: cannot write request."));
        return false;
    }
    return true;
}

void BuzzAsrRunner::pollOutput()
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
                emit failed(QStringLiteral("BuzzASR: %1").arg(response.value(QStringLiteral("error")).toString()));
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
            emit failed(QStringLiteral("BuzzASR stopped unexpectedly. %1").arg(detail));
        }
    }
}
