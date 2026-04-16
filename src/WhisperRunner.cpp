#include "WhisperRunner.h"

#include "TranscriptCleaner.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

WhisperRunner::WhisperRunner(QObject *parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString stdoutText = QString::fromUtf8(process_.readAllStandardOutput());
        const QString stderrText = QString::fromUtf8(process_.readAllStandardError()).trimmed();
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            emit failed(QStringLiteral("whisper-cli failed: %1").arg(stderrText));
            return;
        }
        emit transcriptionReady(TranscriptCleaner::cleanup(stdoutText));
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::UnknownError) {
            emit failed(QStringLiteral("whisper-cli error: %1").arg(process_.errorString()));
        }
    });
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
    process_.start();
}
