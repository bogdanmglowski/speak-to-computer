#include "AppSettings.h"
#include "AudioRecorder.h"
#include "TranscriptCleaner.h"
#include "WavWriter.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class CoreBehaviorTest : public QObject {
    Q_OBJECT

private slots:
    void settingsShouldExpandHomePath();
    void settingsShouldResolveModelLabel();
    void settingsShouldReturnOnlyExistingModelPaths();
    void settingsShouldSaveModelPath();
    void cleanupShouldTrimAndJoinTranscriptLines();
    void wavWriterShouldWritePcm16MonoHeader();
    void recorderAutoShouldPickFirstAvailableBackend();
    void recorderPulseaudioShouldUseParecOrParecord();
    void recorderExplicitUnavailableBackendShouldFail();
    void recorderUnsupportedBackendShouldFail();
    void recorderAutoWithoutToolsShouldReportSupportedBackends();
};

namespace {

QString createExecutable(QTemporaryDir *dir, const QString &name)
{
    const QString path = dir->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write("#!/bin/sh\nexit 0\n");
    file.close();
    QFile::setPermissions(path,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                    QFile::ReadGroup | QFile::ExeGroup |
                    QFile::ReadOther | QFile::ExeOther);
    return path;
}

QString createFile(QTemporaryDir *dir, const QString &name, const QByteArray &content)
{
    const QString path = dir->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(content);
    file.close();
    return path;
}

QString createFile(QTemporaryDir *dir, const QString &name)
{
    return createFile(dir, name, "x");
}

} // namespace

void CoreBehaviorTest::settingsShouldExpandHomePath()
{
    QCOMPARE(AppSettings::expandUserPath(QStringLiteral("~")), QDir::homePath());
    QCOMPARE(AppSettings::expandUserPath(QStringLiteral("~/whisper.cpp/build/bin/whisper-cli")),
            QDir::home().filePath(QStringLiteral("whisper.cpp/build/bin/whisper-cli")));
    QCOMPARE(AppSettings::expandUserPath(QStringLiteral("/opt/whisper-cli")),
            QStringLiteral("/opt/whisper-cli"));
    QCOMPARE(AppSettings::expandUserPath(QStringLiteral("~other/whisper-cli")),
            QStringLiteral("~other/whisper-cli"));
}

void CoreBehaviorTest::settingsShouldResolveModelLabel()
{
    QCOMPARE(AppSettings::modelLabel(QStringLiteral("/tmp/ggml-tiny.bin")), QStringLiteral("tiny"));
    QCOMPARE(AppSettings::modelLabel(QStringLiteral("/tmp/ggml-base.bin")), QStringLiteral("base"));
    QCOMPARE(AppSettings::modelLabel(QStringLiteral("/tmp/ggml-large-v3.bin")), QStringLiteral("large-v3"));
    QCOMPARE(AppSettings::modelLabel(QStringLiteral("/tmp/custom-model.bin")), QStringLiteral("custom-model.bin"));
}

void CoreBehaviorTest::settingsShouldReturnOnlyExistingModelPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!createFile(&dir, QStringLiteral("ggml-large-v3.bin"), QByteArray(12, 'L')).isEmpty());
    QVERIFY(!createFile(&dir, QStringLiteral("ggml-medium.bin"), QByteArray(3, 'M')).isEmpty());
    QVERIFY(!createFile(&dir, QStringLiteral("notes.txt")).isEmpty());
    QVERIFY(!createFile(&dir, QStringLiteral("for-tests-ggml-small.bin")).isEmpty());
    QVERIFY(!createFile(&dir, QStringLiteral("ggml-small.bin")).isEmpty());
    QVERIFY(QFile::resize(dir.filePath(QStringLiteral("ggml-small.bin")), 0));

    const QString currentModelPath = dir.filePath(QStringLiteral("ggml-large.bin"));
    QCOMPARE(AppSettings::existingModelPaths(currentModelPath),
            QStringList({dir.filePath(QStringLiteral("ggml-medium.bin")),
                    dir.filePath(QStringLiteral("ggml-large-v3.bin"))}));
}

void CoreBehaviorTest::settingsShouldSaveModelPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QString errorMessage;
    QVERIFY2(AppSettings::saveModel(settingsPath, QStringLiteral("/tmp/ggml-medium.bin"), &errorMessage),
            qPrintable(errorMessage));

    QSettings settings(settingsPath, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("model")).toString(), QStringLiteral("/tmp/ggml-medium.bin"));
}

void CoreBehaviorTest::cleanupShouldTrimAndJoinTranscriptLines()
{
    const QString raw = QStringLiteral("  To jest test.  \n\n  Druga linia. \r\n");

    QCOMPARE(TranscriptCleaner::cleanup(raw), QStringLiteral("To jest test. Druga linia."));
}

void CoreBehaviorTest::wavWriterShouldWritePcm16MonoHeader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("sample.wav"));
    QString error;
    const QByteArray pcm("\x01\x00\x02\x00", 4);
    QVERIFY2(WavWriter::writePcm16Mono(path, pcm, 16000, &error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray wav = file.readAll();
    QCOMPARE(wav.size(), 48);
    QCOMPARE(wav.mid(0, 4), QByteArray("RIFF", 4));
    QCOMPARE(wav.mid(8, 4), QByteArray("WAVE", 4));
    QCOMPARE(wav.mid(12, 4), QByteArray("fmt ", 4));
    QCOMPARE(wav.mid(36, 4), QByteArray("data", 4));
    QCOMPARE(wav.mid(44), pcm);
}

void CoreBehaviorTest::recorderAutoShouldPickFirstAvailableBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!createExecutable(&dir, QStringLiteral("pw-record")).isEmpty());
    QVERIFY(!createExecutable(&dir, QStringLiteral("parec")).isEmpty());
    QVERIFY(!createExecutable(&dir, QStringLiteral("arecord")).isEmpty());

    QString error;
    const std::optional<AudioRecorderBackend> backend =
            AudioRecorder::selectBackend(QStringLiteral("auto"), {dir.path()}, &error);

    QVERIFY2(backend.has_value(), qPrintable(error));
    QCOMPARE(backend->id, QStringLiteral("pipewire"));
    QCOMPARE(backend->executableName, QStringLiteral("pw-record"));
    QCOMPARE(backend->arguments,
            QStringList({QStringLiteral("--rate"),
                    QStringLiteral("16000"),
                    QStringLiteral("--channels"),
                    QStringLiteral("1"),
                    QStringLiteral("--format"),
                    QStringLiteral("s16"),
                    QStringLiteral("--raw"),
                    QStringLiteral("-")}));
}

void CoreBehaviorTest::recorderPulseaudioShouldUseParecOrParecord()
{
    QTemporaryDir parecordOnlyDir;
    QVERIFY(parecordOnlyDir.isValid());
    QVERIFY(!createExecutable(&parecordOnlyDir, QStringLiteral("parecord")).isEmpty());

    QString error;
    std::optional<AudioRecorderBackend> backend =
            AudioRecorder::selectBackend(QStringLiteral("pulseaudio"), {parecordOnlyDir.path()}, &error);

    QVERIFY2(backend.has_value(), qPrintable(error));
    QCOMPARE(backend->id, QStringLiteral("pulseaudio"));
    QCOMPARE(backend->executableName, QStringLiteral("parecord"));
    QVERIFY(backend->arguments.contains(QStringLiteral("--format=s16le")));

    QTemporaryDir parecPreferredDir;
    QVERIFY(parecPreferredDir.isValid());
    QVERIFY(!createExecutable(&parecPreferredDir, QStringLiteral("parec")).isEmpty());
    QVERIFY(!createExecutable(&parecPreferredDir, QStringLiteral("parecord")).isEmpty());

    backend = AudioRecorder::selectBackend(QStringLiteral("pulseaudio"), {parecPreferredDir.path()}, &error);

    QVERIFY2(backend.has_value(), qPrintable(error));
    QCOMPARE(backend->executableName, QStringLiteral("parec"));
}

void CoreBehaviorTest::recorderExplicitUnavailableBackendShouldFail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const std::optional<AudioRecorderBackend> backend =
            AudioRecorder::selectBackend(QStringLiteral("pipewire"), {dir.path()}, &error);

    QVERIFY(!backend.has_value());
    QVERIFY(error.contains(QStringLiteral("audio_backend=pipewire")));
    QVERIFY(error.contains(QStringLiteral("pw-record")));
}

void CoreBehaviorTest::recorderUnsupportedBackendShouldFail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const std::optional<AudioRecorderBackend> backend =
            AudioRecorder::selectBackend(QStringLiteral("jack"), {dir.path()}, &error);

    QVERIFY(!backend.has_value());
    QVERIFY(error.contains(QStringLiteral("Unsupported audio_backend")));
    QVERIFY(error.contains(QStringLiteral("auto")));
    QVERIFY(error.contains(QStringLiteral("pipewire")));
    QVERIFY(error.contains(QStringLiteral("pulseaudio")));
    QVERIFY(error.contains(QStringLiteral("alsa")));
}

void CoreBehaviorTest::recorderAutoWithoutToolsShouldReportSupportedBackends()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const std::optional<AudioRecorderBackend> backend =
            AudioRecorder::selectBackend(QStringLiteral("auto"), {dir.path()}, &error);

    QVERIFY(!backend.has_value());
    QVERIFY(error.contains(QStringLiteral("pw-record")));
    QVERIFY(error.contains(QStringLiteral("parec")));
    QVERIFY(error.contains(QStringLiteral("parecord")));
    QVERIFY(error.contains(QStringLiteral("arecord")));
}

QTEST_MAIN(CoreBehaviorTest)

#include "CoreBehaviorTest.moc"
