#include "WhisperRunner.h"

#include "TranscriptCleaner.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace {

QString nonEmptyErrorDetails(const QString &stderrText, const QString &stdoutText)
{
    if (!stderrText.isEmpty()) {
        return stderrText;
    }
    if (!stdoutText.isEmpty()) {
        return stdoutText;
    }
    return QStringLiteral("no stderr/stdout output from whisper-cli");
}

QString contextInitializationHint(const QString &details, const QString &modelPath)
{
    if (!details.contains(QStringLiteral("failed to initialize whisper context"), Qt::CaseInsensitive)) {
        return QString();
    }

    return QStringLiteral(
            " Possible cause: not enough RAM for this model or incompatible/corrupted model file (%1)."
            " Try a smaller model (medium/small) or re-download the model.")
            .arg(modelPath);
}

} // namespace

WhisperRunner::WhisperRunner(QObject *parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString stdoutText = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
        const QString stderrText = QString::fromUtf8(process_.readAllStandardError()).trimmed();
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString details = nonEmptyErrorDetails(stderrText, stdoutText);
            const QString hint = contextInitializationHint(details, currentModelPath_);
            const QFileInfo modelInfo(currentModelPath_);
            const qint64 modelSizeMb = modelInfo.exists() ? modelInfo.size() / (1024 * 1024) : -1;

            qWarning().noquote()
                    << "whisper-cli failed"
                    << "exitCode=" << exitCode
                    << "exitStatus=" << (exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit")
                    << "cli=" << currentWhisperCliPath_
                    << "model=" << currentModelPath_
                    << "modelExists=" << modelInfo.exists()
                    << "modelSizeMB=" << modelSizeMb
                    << "wav=" << currentWavPath_
                    << "stderr=" << stderrText
                    << "stdout=" << stdoutText;

            emit failed(QStringLiteral("whisper-cli failed (model: %1): %2%3").arg(currentModelPath_, details, hint));
            return;
        }
        emit transcriptionReady(TranscriptCleaner::cleanup(stdoutText));
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::UnknownError) {
            qWarning().noquote()
                    << "whisper-cli process error"
                    << "error=" << process_.errorString()
                    << "cli=" << currentWhisperCliPath_
                    << "model=" << currentModelPath_
                    << "wav=" << currentWavPath_;
            if (error == QProcess::FailedToStart) {
                emit failed(QStringLiteral("whisper-cli process error (model: %1): %2")
                                    .arg(currentModelPath_, process_.errorString()));
            }
        }
    });
}

WhisperRunner::~WhisperRunner()
{
    if (process_.state() == QProcess::NotRunning) {
        return;
    }

    process_.terminate();
    if (!process_.waitForFinished(1500)) {
        process_.kill();
        process_.waitForFinished(1500);
    }
}

void WhisperRunner::transcribe(const QString &wavPath, const AppSettings &settings)
{
    if (process_.state() != QProcess::NotRunning) {
        emit failed(QStringLiteral("whisper-cli is already running."));
        return;
    }

    const QFileInfo whisperCli(settings.whisperCli);
    const QDir buildDir = QDir(whisperCli.absoluteDir().absoluteFilePath(QStringLiteral("..")));
    QStringList libraryPaths = {
            buildDir.filePath(QStringLiteral("src")),
            buildDir.filePath(QStringLiteral("ggml/src")),
    };

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingLibraryPath = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
    if (!existingLibraryPath.isEmpty()) {
        libraryPaths << existingLibraryPath;
    }
    environment.insert(QStringLiteral("LD_LIBRARY_PATH"), libraryPaths.join(QLatin1Char(':')));

    process_.setProcessEnvironment(environment);
    process_.setProgram(settings.whisperCli);
    process_.setArguments({
            QStringLiteral("-m"),
            settings.model,
            QStringLiteral("-f"),
            wavPath,
            QStringLiteral("-l"),
            settings.language,
            QStringLiteral("-t"),
            QString::number(settings.threads),
            QStringLiteral("-np"),
            QStringLiteral("-nt"),
    });
    process_.setWorkingDirectory(whisperCli.absolutePath());
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    currentWhisperCliPath_ = settings.whisperCli;
    currentModelPath_ = settings.model;
    currentWavPath_ = wavPath;
    process_.start();
}
