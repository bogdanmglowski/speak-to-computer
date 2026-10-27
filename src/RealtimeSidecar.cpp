#include "RealtimeSidecar.h"

#include "ClipboardPaster.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

QString findRecorderExecutable(const QString &requestedBackend)
{
    const QString backend = requestedBackend.trimmed().toLower();
    auto findFirst = [](const QStringList &names) -> QString {
        for (const QString &name : names) {
            const QString path = QStandardPaths::findExecutable(name);
            if (!path.isEmpty()) {
                return path;
            }
        }
        return {};
    };

    if (backend == QStringLiteral("pipewire") || backend == QStringLiteral("auto") || backend.isEmpty()) {
        const QString pw = findFirst({QStringLiteral("pw-record")});
        if (!pw.isEmpty() || backend == QStringLiteral("pipewire")) {
            return pw;
        }
    }
    if (backend == QStringLiteral("pulseaudio") || backend == QStringLiteral("auto") || backend.isEmpty()) {
        const QString pulse = findFirst({QStringLiteral("parec"), QStringLiteral("parecord")});
        if (!pulse.isEmpty()) {
            return pulse;
        }
    }
    return findFirst({QStringLiteral("arecord")});
}

bool pidIsAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    if (QFile::exists(QStringLiteral("/proc/%1").arg(pid))) {
        QFile status(QStringLiteral("/proc/%1/status").arg(pid));
        if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray data = status.readAll();
            if (data.contains("\nState:\tZ") || data.startsWith("State:\tZ")) {
                return false;
            }
        }
        return true;
    }
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

QString shellQuote(const QString &value)
{
    // Safe single-quote wrapping for bash -c.
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

RealtimeSidecar::RealtimeSidecar(QObject *parent)
    : QObject(parent)
{
    connect(&micProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &RealtimeSidecar::handleMicFinished);
    connect(&socket_, &QTcpSocket::readyRead, this, &RealtimeSidecar::handleSocketReadyRead);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &RealtimeSidecar::handleSocketError);
}

RealtimeSidecar::~RealtimeSidecar()
{
    stopSession();
    unloadServer();
}

bool RealtimeSidecar::isServerRunning() const
{
    return isServerPidAlive();
}

bool RealtimeSidecar::isServerPidAlive() const
{
    return pidIsAlive(serverPid_);
}

bool RealtimeSidecar::isSessionActive() const
{
    return sessionActive_;
}

QString RealtimeSidecar::defaultServerLogPath() const
{
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString base = cache;
    if (base.isEmpty()) {
        base = QDir::home().filePath(QStringLiteral(".cache/speak-to-computer"));
    }
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("realtime-server.log"));
}

QString RealtimeSidecar::resolveWrapperScript() const
{
#ifdef SPEAK_TO_COMPUTER_SOURCE_DIR
    const QString fromSource = QDir(QStringLiteral(SPEAK_TO_COMPUTER_SOURCE_DIR))
                                       .filePath(QStringLiteral("scripts/simulstreaming-poc/run-server-for-app.sh"));
    if (QFileInfo::exists(fromSource)) {
        return fromSource;
    }
#endif
    const QString nextToBinary = QDir(QCoreApplication::applicationDirPath())
                                         .filePath(QStringLiteral("run-server-for-app.sh"));
    if (QFileInfo::exists(nextToBinary)) {
        return nextToBinary;
    }
    return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../scripts/simulstreaming-poc/run-server-for-app.sh"));
}

void RealtimeSidecar::killProcessesOnPort(int port) const
{
    if (port <= 0) {
        return;
    }
    QProcess killer;
    killer.start(QStringLiteral("ss"),
            {QStringLiteral("-ltnp"), QStringLiteral("sport = :%1").arg(port)});
    if (!killer.waitForFinished(2000)) {
        return;
    }
    const QString out = QString::fromUtf8(killer.readAllStandardOutput());
    static const QRegularExpression pidRe(QStringLiteral("pid=(\\d+)"));
    QRegularExpressionMatchIterator it = pidRe.globalMatch(out);
    QSet<qint64> pids;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        pids.insert(match.captured(1).toLongLong());
    }
    for (qint64 pid : pids) {
        if (pid > 0) {
            ::kill(static_cast<pid_t>(pid), SIGTERM);
        }
    }
    QThread::msleep(400);
    for (qint64 pid : pids) {
        if (pid > 0) {
            ::kill(static_cast<pid_t>(pid), SIGKILL);
        }
    }
}

void RealtimeSidecar::killServerProcessTree()
{
    const qint64 pid = serverPid_;
    if (pid > 0) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
        ::kill(-static_cast<pid_t>(pid), SIGTERM);
        for (int i = 0; i < 20 && pidIsAlive(pid); ++i) {
            QThread::msleep(100);
        }
        if (pidIsAlive(pid)) {
            qWarning() << "realtime server did not exit on TERM, killing" << pid;
            ::kill(static_cast<pid_t>(pid), SIGKILL);
            ::kill(-static_cast<pid_t>(pid), SIGKILL);
            QThread::msleep(200);
        }
    }
    serverPid_ = 0;
}

QString RealtimeSidecar::drainServerLogFile()
{
    if (serverLogPath_.isEmpty()) {
        return {};
    }
    QFile file(serverLogPath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    if (serverLogReadOffset_ > file.size()) {
        serverLogReadOffset_ = 0;
    }
    if (!file.seek(serverLogReadOffset_)) {
        return {};
    }
    const QByteArray chunk = file.readAll();
    serverLogReadOffset_ = file.pos();
    if (chunk.isEmpty()) {
        return {};
    }
    const QString text = QString::fromUtf8(chunk);
    serverLogTail_ += text;
    if (serverLogTail_.size() > 12000) {
        serverLogTail_ = serverLogTail_.right(12000);
    }
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        qInfo().noquote() << "[realtime-server]" << line.trimmed();
    }
    return text;
}

bool RealtimeSidecar::waitForServerReady(int timeoutMs, QString *errorMessage)
{
    // IMPORTANT: do not spin Qt local event loops (waitForConnected/processEvents)
    // while the GPU server is loading. Under NoMachine/KDE that starves/deadlocks
    // CUDA init in the child even when the child is reparented via nohup.
    // Use plain sleeps + non-blocking TCP checks + brief UI yields only.
    const int stepMs = 500;
    int waited = 0;
    int nextProgressLogMs = 3000;
    while (waited < timeoutMs) {
        const QString newLog = drainServerLogFile();
        if (newLog.contains(QStringLiteral("Listening on"), Qt::CaseInsensitive)
                || serverLogTail_.contains(QStringLiteral("Listening on"), Qt::CaseInsensitive)) {
            // Short connect only after log says ready.
            QTcpSocket probe;
            probe.connectToHost(host_, static_cast<quint16>(port_));
            if (probe.waitForConnected(300)) {
                probe.disconnectFromHost();
                qInfo().noquote() << "realtime port open after" << waited << "ms (log+tcp)";
                return true;
            }
        }

        const bool procAlive = isServerPidAlive();
        if (!procAlive) {
            drainServerLogFile();
            const QString tail = serverLogTail_.trimmed();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                        "Realtime server exited before opening port %1:%2 (pid was %3).\n"
                        "Log file: %4\n%5")
                                        .arg(host_)
                                        .arg(port_)
                                        .arg(serverPid_)
                                        .arg(serverLogPath_)
                                        .arg(tail.isEmpty() ? QStringLiteral("(empty log)") : tail);
            }
            qWarning().noquote() << "realtime server died during startup after" << waited << "ms";
            serverPid_ = 0;
            return false;
        }

        // Non-blocking-ish TCP probe (1 ms) so we don't hold a nested event loop.
        {
            QTcpSocket probe;
            probe.connectToHost(host_, static_cast<quint16>(port_));
            if (probe.waitForConnected(1)) {
                probe.disconnectFromHost();
                drainServerLogFile();
                qInfo().noquote() << "realtime port open after" << waited << "ms";
                return true;
            }
        }

        waited += stepMs;
        if (waited >= nextProgressLogMs) {
            qInfo().noquote() << "waiting for realtime server on" << host_ << port_
                              << "…" << waited << "/" << timeoutMs << "ms"
                              << "pid" << serverPid_
                              << "alive" << procAlive
                              << "log" << serverLogPath_;
            nextProgressLogMs += 5000;
        }

        // Sleep outside Qt socket waiters. Tiny processEvents so tray stays responsive
        // without continuously pumping GL/compositor work.
        QThread::msleep(static_cast<unsigned long>(stepMs));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
    }

    drainServerLogFile();
    const QString tail = serverLogTail_.trimmed();
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral(
                "Realtime server did not open %1:%2 within %3 ms (process still running=%4, pid=%5).\n"
                "Log file: %6\n%7")
                                .arg(host_)
                                .arg(port_)
                                .arg(timeoutMs)
                                .arg(isServerPidAlive() ? QStringLiteral("yes") : QStringLiteral("no"))
                                .arg(serverPid_)
                                .arg(serverLogPath_)
                                .arg(tail.isEmpty() ? QStringLiteral("(empty log)") : tail);
    }
    return false;
}

bool RealtimeSidecar::loadServer(const AppSettings &settings, QString *errorMessage)
{
    if (loading_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Realtime server load already in progress.");
        }
        return false;
    }

    const QString dictionaryPrompt = AppSettings::loadDictionaryPrompt(settings.dictionaryPath);
    if (isServerRunning()
            && loadedModel_ == settings.realtimeModel
            && language_ == settings.language
            && host_ == settings.realtimeHost
            && port_ == settings.realtimePort
            && loadedDictionaryPrompt_ == dictionaryPrompt) {
        return true;
    }

    loading_ = true;
    struct LoadingGuard {
        bool &flag;
        ~LoadingGuard() { flag = false; }
    } guard{loading_};

    unloadServer();

    const QString python = settings.realtimePython.trimmed();
    const QString script = settings.realtimeServerScript.trimmed();
    if (!QFileInfo::exists(python) || !QFileInfo(python).isExecutable()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Realtime Python not found/executable: %1\n"
                                           "Run scripts/simulstreaming-poc/setup.sh first.")
                                    .arg(python);
        }
        return false;
    }
    if (!QFileInfo::exists(script)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Realtime server script missing: %1").arg(script);
        }
        return false;
    }

    host_ = settings.realtimeHost;
    port_ = settings.realtimePort;
    language_ = settings.language.trimmed().isEmpty() ? QStringLiteral("pl") : settings.language.trimmed();
    if (language_.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
            || language_.compare(QStringLiteral("detect"), Qt::CaseInsensitive) == 0) {
        language_ = QStringLiteral("auto");
    } else if (language_.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) {
        language_ = QStringLiteral("en");
    } else if (language_.startsWith(QStringLiteral("pl"), Qt::CaseInsensitive)) {
        language_ = QStringLiteral("pl");
    }
    loadedModel_ = settings.realtimeModel.trimmed().isEmpty() ? QStringLiteral("medium")
                                                              : settings.realtimeModel.trimmed();
    loadedDictionaryPrompt_ = dictionaryPrompt;

    killProcessesOnPort(port_);
    serverLogTail_.clear();
    serverLogReadOffset_ = 0;
    serverLogPath_ = defaultServerLogPath();

    const QString wrapper = resolveWrapperScript();
    if (!QFileInfo::exists(wrapper)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Realtime wrapper missing: %1").arg(wrapper);
        }
        return false;
    }
    QFile::setPermissions(wrapper,
            QFile::permissions(wrapper) | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup
                    | QFileDevice::ExeOther);

    Q_UNUSED(python);
    Q_UNUSED(script);

    // Launch via nohup so the server is reparented off the Qt process tree.
    // Direct QProcess/startDetached children hang in torch/CUDA init under this
    // NoMachine+KDE session; a sibling shell or nohup'd process starts in ~8s.
    // Wrapper itself sanitizes LD_PRELOAD / DISPLAY / env and writes the log file.
    // Arg 7 = dictionary file path (optional); wrapper builds --static_init_prompt.
    const QString dictionaryPath = settings.dictionaryPath.trimmed();
    const QString shellCmd = QStringLiteral(
            "nohup bash %1 %2 %3 %4 %5 %6 %7 >/dev/null 2>&1 & echo $!")
                                     .arg(shellQuote(wrapper),
                                             shellQuote(host_),
                                             shellQuote(QString::number(port_)),
                                             shellQuote(loadedModel_),
                                             shellQuote(language_),
                                             shellQuote(serverLogPath_),
                                             shellQuote(dictionaryPath));

    QProcess starter;
    starter.setProcessChannelMode(QProcess::SeparateChannels);
    starter.start(QStringLiteral("bash"), {QStringLiteral("-c"), shellCmd});
    if (!starter.waitForStarted(3000) || !starter.waitForFinished(5000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start realtime wrapper launcher: %1")
                                    .arg(starter.errorString());
        }
        qWarning().noquote() << "realtime launcher failed:" << starter.errorString();
        return false;
    }
    const QByteArray out = starter.readAllStandardOutput().trimmed();
    bool pidOk = false;
    serverPid_ = out.toLongLong(&pidOk);
    if (!pidOk || serverPid_ <= 0 || !pidIsAlive(serverPid_)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Realtime launcher did not return a live pid (got %1).")
                                    .arg(QString::fromUtf8(out));
        }
        qWarning().noquote() << "realtime launcher bad pid output:" << out;
        serverPid_ = 0;
        return false;
    }
    qInfo().noquote() << "starting realtime server via nohup wrapper"
                      << "wrapper=" << wrapper
                      << "model=" << loadedModel_
                      << "listen=" << host_ << port_
                      << "log=" << serverLogPath_
                      << "dictionaryPromptChars=" << loadedDictionaryPrompt_.size()
                      << "pid=" << serverPid_;

    if (!waitForServerReady(180000, errorMessage)) {
        qWarning().noquote() << "realtime server failed readiness:"
                             << (errorMessage != nullptr ? *errorMessage : QStringLiteral("(no detail)"));
        unloadServer();
        return false;
    }

    qInfo().noquote() << "realtime server ready" << loadedModel_ << "pid" << serverPid_;
    emit serverReady();
    return true;
}

void RealtimeSidecar::unloadServer()
{
    loadedDictionaryPrompt_.clear();
    qInfo().noquote() << "realtime server unload requested"
                      << "running=" << isServerPidAlive()
                      << "pid=" << serverPid_
                      << "sessionActive=" << sessionActive_;
    stopSession();
    if (!isServerPidAlive()) {
        killProcessesOnPort(port_);
        serverPid_ = 0;
        loadedModel_.clear();
        qInfo() << "realtime server already stopped (port cleaned)";
        return;
    }
    drainServerLogFile();
    killServerProcessTree();
    killProcessesOnPort(port_);
    loadedModel_.clear();
    qInfo() << "realtime server unloaded";
}

bool RealtimeSidecar::startSession(quint64 targetWindow, const AppSettings &settings, QString *errorMessage)
{
    if (!isServerRunning()) {
        if (!loadServer(settings, errorMessage)) {
            return false;
        }
    }
    if (sessionActive_) {
        stopSession();
    }

    targetWindow_ = targetWindow;
    pinPasteTarget_ = settings.isPinPasteTarget();
    lastEmittedChar_ = QChar();
    pasteTail_.clear();
    sendKeywordEnabled_ = settings.sendKeywordEnabled;
    transcription_.clear();
    sendKeyword_ = settings.sendKeyword.trimmed();
    if (sendKeyword_.isEmpty()) {
        sendKeyword_ = QStringLiteral("go");
    }
    socketBuffer_.clear();
    sessionActive_ = true;

    socket_.connectToHost(host_, static_cast<quint16>(port_));
    if (!socket_.waitForConnected(3000)) {
        sessionActive_ = false;
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not connect to realtime server at %1:%2: %3")
                                    .arg(host_)
                                    .arg(port_)
                                    .arg(socket_.errorString());
        }
        return false;
    }

    if (!startMicProcess(settings.audioBackend, errorMessage)) {
        socket_.disconnectFromHost();
        sessionActive_ = false;
        return false;
    }

    connect(&micProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!sessionActive_ || socket_.state() != QAbstractSocket::ConnectedState) {
            return;
        }
        const QByteArray chunk = micProcess_.readAllStandardOutput();
        if (!chunk.isEmpty()) {
            socket_.write(chunk);
        }
    });

    qInfo() << "realtime session started";
    return true;
}

bool RealtimeSidecar::startMicProcess(const QString &audioBackend, QString *errorMessage)
{
    const QString recorder = findRecorderExecutable(audioBackend);
    if (recorder.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No audio recorder found (pw-record / parec / arecord).");
        }
        return false;
    }

    QStringList args;
    const QString name = QFileInfo(recorder).fileName();
    if (name == QStringLiteral("pw-record")) {
        args = {QStringLiteral("--rate=16000"),
                QStringLiteral("--channels=1"),
                QStringLiteral("--format=s16"),
                QStringLiteral("--raw"),
                QStringLiteral("-")};
    } else if (name == QStringLiteral("parec") || name == QStringLiteral("parecord")) {
        args = {QStringLiteral("--rate=16000"),
                QStringLiteral("--channels=1"),
                QStringLiteral("--format=s16le"),
                QStringLiteral("--raw"),
                QStringLiteral("--file-format=raw")};
    } else {
        args = {QStringLiteral("-f"),
                QStringLiteral("S16_LE"),
                QStringLiteral("-c1"),
                QStringLiteral("-r"),
                QStringLiteral("16000"),
                QStringLiteral("-t"),
                QStringLiteral("raw"),
                QStringLiteral("-D"),
                QStringLiteral("default")};
    }

    micProcess_.setProcessChannelMode(QProcess::SeparateChannels);
    micProcess_.start(recorder, args);
    if (!micProcess_.waitForStarted(2000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start mic recorder %1: %2")
                                    .arg(recorder, micProcess_.errorString());
        }
        return false;
    }
    return true;
}

void RealtimeSidecar::setSendKeywordEnabled(bool enabled)
{
    sendKeywordEnabled_ = enabled;
}

void RealtimeSidecar::setPasteTarget(const QString &pasteTarget)
{
    pinPasteTarget_ = AppSettings::normalizePasteTarget(pasteTarget)
            == QString::fromUtf8(AppSettings::kPasteTargetPin);
}

void RealtimeSidecar::setTargetWindow(quint64 targetWindow)
{
    targetWindow_ = targetWindow;
}

void RealtimeSidecar::stopSession()
{
    if (!sessionActive_ && micProcess_.state() == QProcess::NotRunning
            && socket_.state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    sessionActive_ = false;

    disconnect(&micProcess_, &QProcess::readyReadStandardOutput, this, nullptr);
    if (micProcess_.state() != QProcess::NotRunning) {
        micProcess_.terminate();
        if (!micProcess_.waitForFinished(1500)) {
            micProcess_.kill();
            micProcess_.waitForFinished(1000);
        }
    }
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.disconnectFromHost();
        if (socket_.state() != QAbstractSocket::UnconnectedState) {
            socket_.waitForDisconnected(500);
        }
    }
    socketBuffer_.clear();
    emit sessionFinished();
    qInfo() << "realtime session stopped";
}

QString RealtimeSidecar::extractTextFromServerLine(const QString &line) const
{
    static const QRegularExpression re(
            QStringLiteral(R"(^\s*(\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s(.*)$)"));
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch()) {
        return {};
    }
    return match.captured(3);
}

QString RealtimeSidecar::ensureSpacing(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    if (lastEmittedChar_.isNull() || lastEmittedChar_.isSpace() || text.at(0).isSpace()) {
        return text;
    }
    const QChar first = text.at(0);
    static const QString noSpaceBefore = QStringLiteral(",.;:!?…)]}»\"'");
    static const QString noSpaceAfter = QStringLiteral("([{«\"'");
    if (noSpaceBefore.contains(first)) {
        return text;
    }
    if (noSpaceAfter.contains(lastEmittedChar_)) {
        return text;
    }
    if (lastEmittedChar_.isLetterOrNumber() && (first.isLetterOrNumber() || first == QLatin1Char('('))) {
        return QLatin1Char(' ') + text;
    }
    if (QStringLiteral(".!?").contains(lastEmittedChar_) && first.isLetterOrNumber()) {
        return QLatin1Char(' ') + text;
    }
    return text;
}

bool RealtimeSidecar::sendKeys(const QStringList &keys, QString *errorMessage)
{
    if (keys.isEmpty()) {
        return true;
    }
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (xdotool.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool not found.");
        }
        return false;
    }

    QStringList args;
    if (pinPasteTarget_ && targetWindow_ != 0) {
        args << QStringLiteral("windowactivate") << QStringLiteral("--sync") << QString::number(targetWindow_);
    }
    args << QStringLiteral("key") << QStringLiteral("--clearmodifiers");
    args << keys;

    QProcess keyProcess;
    keyProcess.start(xdotool, args);
    if (!keyProcess.waitForFinished(2500) || keyProcess.exitCode() != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool key failed: %1")
                                    .arg(QString::fromUtf8(keyProcess.readAllStandardError()));
        }
        return false;
    }
    return true;
}

bool RealtimeSidecar::sendEnter(QString *errorMessage)
{
    return sendKeys({QStringLiteral("Return")}, errorMessage);
}

bool RealtimeSidecar::sendBackspaces(int count, QString *errorMessage)
{
    if (count <= 0) {
        return true;
    }
    QStringList keys;
    keys.reserve(count);
    for (int i = 0; i < count; ++i) {
        keys << QStringLiteral("BackSpace");
    }
    return sendKeys(keys, errorMessage);
}

bool RealtimeSidecar::applySendKeyword(QString *text, int *backspacesOut)
{
    if (backspacesOut != nullptr) {
        *backspacesOut = 0;
    }
    if (!sendKeywordEnabled_ || sendKeyword_.isEmpty() || text == nullptr) {
        return false;
    }

    const QString combined = pasteTail_ + *text;
    // Whole word at end: optional space before keyword, optional trailing punct/space.
    const QString pattern = QStringLiteral("(?i)(?:^|[\\s])(%1)([.!?…,;:]*)\\s*$")
                                    .arg(QRegularExpression::escape(sendKeyword_));
    const QRegularExpression re(pattern);
    const QRegularExpressionMatch match = re.match(combined);
    if (!match.hasMatch()) {
        return false;
    }

    // Strip from optional whitespace before keyword through end.
    int stripFrom = match.capturedStart(1);
    if (stripFrom > 0 && combined.at(stripFrom - 1).isSpace()) {
        --stripFrom;
    }

    const int alreadyPasted = pasteTail_.size();
    int backspaces = 0;
    if (stripFrom < alreadyPasted) {
        backspaces = alreadyPasted - stripFrom;
    }
    if (backspacesOut != nullptr) {
        *backspacesOut = backspaces;
    }

    // Keep only the part of this delta that is before the keyword strip point.
    const int keepInIncoming = qMax(0, stripFrom - alreadyPasted);
    *text = text->left(keepInIncoming);
    return true;
}

bool RealtimeSidecar::pasteDelta(const QString &text, QString *errorMessage)
{
    if (text.isEmpty()) {
        return true;
    }
    const QString xclip = QStandardPaths::findExecutable(QStringLiteral("xclip"));
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (xclip.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xclip not found (needed for realtime paste).");
        }
        return false;
    }
    if (xdotool.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool not found.");
        }
        return false;
    }

    QProcess clip;
    clip.start(xclip, {QStringLiteral("-selection"), QStringLiteral("clipboard")});
    if (!clip.waitForStarted(1000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start xclip.");
        }
        return false;
    }
    clip.write(text.toUtf8());
    clip.closeWriteChannel();
    if (!clip.waitForFinished(1500)) {
        clip.kill();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xclip timed out.");
        }
        return false;
    }

    QStringList args;
    const bool pinWindow = pinPasteTarget_ && targetWindow_ != 0;
    if (pinWindow) {
        args << QStringLiteral("windowactivate") << QStringLiteral("--sync") << QString::number(targetWindow_);
    }
    // Focus mode does not activate the start window. The key lands in the
    // window that has focus now, so the shortcut must match that window.
    const quint64 keystrokeWindow = pinWindow ? targetWindow_ : 0;
    const QString keystroke = ClipboardPaster::pasteKeystrokeFor(keystrokeWindow);
    qInfo().noquote() << "realtime paste window" << keystrokeWindow << "key" << keystroke;
    args << QStringLiteral("key") << QStringLiteral("--clearmodifiers") << keystroke;

    QProcess paste;
    paste.start(xdotool, args);
    if (!paste.waitForFinished(2500) || paste.exitCode() != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool paste failed: %1")
                                    .arg(QString::fromUtf8(paste.readAllStandardError()));
        }
        return false;
    }
    return true;
}

void RealtimeSidecar::handleSocketReadyRead()
{
    socketBuffer_.append(socket_.readAll());
    while (true) {
        const int newline = socketBuffer_.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray rawLine = socketBuffer_.left(newline);
        socketBuffer_.remove(0, newline + 1);
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QString text = extractTextFromServerLine(line);
        if (text.isEmpty()) {
            continue;
        }
        text = ensureSpacing(text);
        if (text.isEmpty()) {
            continue;
        }

        int backspaces = 0;
        const bool sendEnterNow = applySendKeyword(&text, &backspaces);

        transcription_.chop(backspaces);
        transcription_ += text;
        emit transcriptionUpdated(transcription_);

        QString errorMessage;
        if (backspaces > 0 && !sendBackspaces(backspaces, &errorMessage)) {
            qWarning().noquote() << errorMessage;
            emit sessionFailed(errorMessage);
            stopSession();
            return;
        }
        if (!text.isEmpty()) {
            if (!pasteDelta(text, &errorMessage)) {
                qWarning().noquote() << errorMessage;
                emit sessionFailed(errorMessage);
                stopSession();
                return;
            }
            lastEmittedChar_ = text.at(text.size() - 1);
            pasteTail_ = (pasteTail_ + text).right(64);
            emit partialText(text);
        } else if (backspaces > 0) {
            pasteTail_ = pasteTail_.left(qMax(0, pasteTail_.size() - backspaces));
        }

        if (sendEnterNow) {
            if (!sendEnter(&errorMessage)) {
                qWarning().noquote() << errorMessage;
                emit sessionFailed(errorMessage);
                stopSession();
                return;
            }
            // Fresh line after Enter — don't carry keyword tail.
            pasteTail_.clear();
            lastEmittedChar_ = QChar();
            qInfo().noquote() << "send keyword triggered Enter:" << sendKeyword_;
            emit partialText(QStringLiteral("\n"));
            transcription_ += QLatin1Char('\n');
            emit transcriptionUpdated(transcription_);
        }
    }
}

void RealtimeSidecar::handleSocketError(QAbstractSocket::SocketError error)
{
    if (!sessionActive_) {
        return;
    }
    Q_UNUSED(error);
    const QString message = QStringLiteral("Realtime socket error: %1").arg(socket_.errorString());
    qWarning().noquote() << message;
    emit sessionFailed(message);
    stopSession();
}

void RealtimeSidecar::handleMicFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!sessionActive_) {
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString err = QString::fromUtf8(micProcess_.readAllStandardError()).trimmed();
        const QString message = QStringLiteral("Mic recorder stopped: %1").arg(err.isEmpty() ? QString::number(exitCode) : err);
        emit sessionFailed(message);
    }
    stopSession();
}
