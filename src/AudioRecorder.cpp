#include "AudioRecorder.h"

#include <QtEndian>

#include <QList>
#include <QStandardPaths>

#include <cmath>

namespace {

struct BackendCandidate {
    QString id;
    QString displayName;
    QString executableName;
    QStringList arguments;
};

QList<BackendCandidate> backendCandidates()
{
    return {
            {
                    QStringLiteral("pipewire"),
                    QStringLiteral("PipeWire pw-record"),
                    QStringLiteral("pw-record"),
                    {
                            QStringLiteral("--rate"),
                            QStringLiteral("16000"),
                            QStringLiteral("--channels"),
                            QStringLiteral("1"),
                            QStringLiteral("--format"),
                            QStringLiteral("s16"),
                            QStringLiteral("--raw"),
                            QStringLiteral("-"),
                    },
            },
            {
                    QStringLiteral("pulseaudio"),
                    QStringLiteral("PulseAudio parec"),
                    QStringLiteral("parec"),
                    {
                            QStringLiteral("--rate=16000"),
                            QStringLiteral("--channels=1"),
                            QStringLiteral("--format=s16le"),
                            QStringLiteral("--raw"),
                    },
            },
            {
                    QStringLiteral("pulseaudio"),
                    QStringLiteral("PulseAudio parecord"),
                    QStringLiteral("parecord"),
                    {
                            QStringLiteral("--rate=16000"),
                            QStringLiteral("--channels=1"),
                            QStringLiteral("--format=s16le"),
                            QStringLiteral("--raw"),
                    },
            },
            {
                    QStringLiteral("alsa"),
                    QStringLiteral("ALSA arecord"),
                    QStringLiteral("arecord"),
                    {
                            QStringLiteral("-q"),
                            QStringLiteral("-t"),
                            QStringLiteral("raw"),
                            QStringLiteral("-f"),
                            QStringLiteral("S16_LE"),
                            QStringLiteral("-c"),
                            QStringLiteral("1"),
                            QStringLiteral("-r"),
                            QStringLiteral("16000"),
                    },
            },
    };
}

QStringList supportedBackendIds()
{
    return {
            QStringLiteral("pipewire"),
            QStringLiteral("pulseaudio"),
            QStringLiteral("alsa"),
    };
}

QString normalizedBackend(const QString &requestedBackend)
{
    const QString trimmed = requestedBackend.trimmed().toLower();
    if (trimmed.isEmpty()) {
        return QStringLiteral("auto");
    }
    return trimmed;
}

QString findExecutable(const QString &executableName, const QStringList &searchPaths)
{
    if (searchPaths.isEmpty()) {
        return QStandardPaths::findExecutable(executableName);
    }
    return QStandardPaths::findExecutable(executableName, searchPaths);
}

QString missingToolsMessage()
{
    return QStringLiteral("No supported audio recorder found. Install PipeWire tools (pw-record), "
                          "PulseAudio tools (parec or parecord), or ALSA utils (arecord).");
}

QString unsupportedBackendMessage(const QString &requestedBackend)
{
    return QStringLiteral("Unsupported audio_backend '%1'. Use one of: auto, pipewire, pulseaudio, alsa.")
            .arg(requestedBackend);
}

QString explicitBackendMissingMessage(const QString &backend)
{
    if (backend == QStringLiteral("pipewire")) {
        return QStringLiteral("audio_backend=pipewire requires pw-record in PATH.");
    }
    if (backend == QStringLiteral("pulseaudio")) {
        return QStringLiteral("audio_backend=pulseaudio requires parec or parecord in PATH.");
    }
    if (backend == QStringLiteral("alsa")) {
        return QStringLiteral("audio_backend=alsa requires arecord in PATH.");
    }
    return unsupportedBackendMessage(backend);
}

AudioRecorderBackend toBackend(const BackendCandidate &candidate, const QString &program)
{
    return {
            candidate.id,
            candidate.displayName,
            candidate.executableName,
            program,
            candidate.arguments,
    };
}

} // namespace

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, &AudioRecorder::readPendingAudio);
    connect(&process_, &QProcess::finished, this, &AudioRecorder::handleProcessFinished);
    connect(&process_, &QProcess::errorOccurred, this, &AudioRecorder::handleProcessError);
}

AudioRecorder::~AudioRecorder()
{
    if (process_.state() == QProcess::NotRunning) {
        return;
    }

    stopping_ = true;
    process_.terminate();
    if (!process_.waitForFinished(1500)) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}

bool AudioRecorder::start(QString *errorMessage)
{
    return start(QStringLiteral("auto"), errorMessage);
}

bool AudioRecorder::start(const QString &requestedBackend, QString *errorMessage)
{
    if (process_.state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Recording is already running.");
        }
        return false;
    }

    const std::optional<AudioRecorderBackend> backend = selectBackend(requestedBackend, errorMessage);
    if (!backend.has_value()) {
        return false;
    }

    pcm_.clear();
    stopping_ = false;
    activeBackend_ = *backend;

    process_.setProgram(backend->program);
    process_.setArguments(backend->arguments);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    process_.start();

    if (!process_.waitForStarted(2000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start %1: %2")
                    .arg(backend->displayName, process_.errorString());
        }
        activeBackend_.reset();
        return false;
    }

    return true;
}

QByteArray AudioRecorder::stop(QString *errorMessage)
{
    if (process_.state() == QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Recording is not running.");
        }
        return pcm_;
    }

    stopping_ = true;
    process_.terminate();
    if (!process_.waitForFinished(1500)) {
        process_.kill();
        process_.waitForFinished(1000);
    }

    readPendingAudio();
    stopping_ = false;
    return pcm_;
}

bool AudioRecorder::isRecording() const
{
    return process_.state() != QProcess::NotRunning;
}

QString AudioRecorder::activeBackendName() const
{
    if (!activeBackend_.has_value()) {
        return QStringLiteral("none");
    }
    return QStringLiteral("%1 (%2)").arg(activeBackend_->id, activeBackend_->executableName);
}

std::optional<AudioRecorderBackend> AudioRecorder::selectBackend(
        const QString &requestedBackend,
        QString *errorMessage)
{
    return selectBackend(requestedBackend, QStringList(), errorMessage);
}

std::optional<AudioRecorderBackend> AudioRecorder::selectBackend(
        const QString &requestedBackend,
        const QStringList &searchPaths,
        QString *errorMessage)
{
    const QString backend = normalizedBackend(requestedBackend);
    const QStringList supported = supportedBackendIds();
    if (backend != QStringLiteral("auto") && !supported.contains(backend)) {
        if (errorMessage != nullptr) {
            *errorMessage = unsupportedBackendMessage(requestedBackend);
        }
        return std::nullopt;
    }

    for (const BackendCandidate &candidate : backendCandidates()) {
        if (backend != QStringLiteral("auto") && candidate.id != backend) {
            continue;
        }

        const QString program = findExecutable(candidate.executableName, searchPaths);
        if (!program.isEmpty()) {
            return toBackend(candidate, program);
        }
    }

    if (errorMessage != nullptr) {
        *errorMessage = backend == QStringLiteral("auto")
                ? missingToolsMessage()
                : explicitBackendMissingMessage(backend);
    }
    return std::nullopt;
}

bool AudioRecorder::hasSupportedBackend(const QString &requestedBackend, QString *errorMessage)
{
    return selectBackend(requestedBackend, errorMessage).has_value();
}

void AudioRecorder::readPendingAudio()
{
    const QByteArray chunk = process_.readAllStandardOutput();
    if (chunk.isEmpty()) {
        return;
    }

    pcm_.append(chunk);
    emit levelChanged(levelFromPcm16(chunk));
}

void AudioRecorder::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    readPendingAudio();
    if (stopping_) {
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrText = QString::fromUtf8(process_.readAllStandardError()).trimmed();
        const QString details = stderrText.isEmpty() ? QStringLiteral("no details") : stderrText;
        emit failed(QStringLiteral("%1 stopped unexpectedly: %2")
                            .arg(activeBackendName(), details));
    }
}

void AudioRecorder::handleProcessError(QProcess::ProcessError error)
{
    if (stopping_ || error == QProcess::UnknownError) {
        return;
    }
    emit failed(QStringLiteral("%1 error: %2").arg(activeBackendName(), process_.errorString()));
}

double AudioRecorder::levelFromPcm16(const QByteArray &chunk)
{
    const auto *data = reinterpret_cast<const uchar *>(chunk.constData());
    const qsizetype sampleCount = chunk.size() / 2;
    if (sampleCount == 0) {
        return 0.0;
    }

    double squareSum = 0.0;
    for (qsizetype i = 0; i < sampleCount; ++i) {
        const qint16 sample = qFromLittleEndian<qint16>(data + i * 2);
        const double normalized = static_cast<double>(sample) / 32768.0;
        squareSum += normalized * normalized;
    }

    const double rms = std::sqrt(squareSum / static_cast<double>(sampleCount));
    return std::min(1.0, rms * 8.0);
}
