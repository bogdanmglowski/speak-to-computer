#include "AppSettings.h"
#include "AudioRecorder.h"
#include "TranscriptCleaner.h"
#include "VadEndpointDetector.h"
#include "WavWriter.h"
#include "WhisperRunner.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <QtEndian>

#include <memory>

class CoreBehaviorTest : public QObject {
    Q_OBJECT

private slots:
    void settingsShouldExpandHomePath();
    void settingsShouldResolveModelLabel();
    void settingsShouldReturnOnlyExistingModelPaths();
    void settingsShouldSaveModelPath();
    void settingsShouldLoadSeparateHotkeyDefaults();
    void settingsShouldMigrateLegacyHotkeyToDictationHotkey();
    void settingsShouldIgnoreTranslateToEnglishAsSavedMode();
    void settingsShouldLoadWakeWordDefaults();
    void settingsShouldLoadWakeWordOverrides();
    void settingsShouldSaveWakeWordEnabled();
    void settingsShouldLoadVadDefaults();
    void settingsShouldLoadVadOverrides();
    void settingsShouldSaveVadAutostopEnabled();
    void settingsShouldMigrateLegacyConfigWithoutVadKeys();
    void settingsShouldMigrateLegacyWakeWordRuntimeDefaults();
    void settingsShouldMigrateAppDataWakeWordRuntimeDefaults();
    void cleanupShouldTrimAndJoinTranscriptLines();
    void cleanupShouldDropWhisperNonSpeechAnnotations();
    void whisperRunnerShouldPassTranslateFlagWhenEnabled();
    void whisperRunnerShouldOmitTranslateFlagWhenDisabled();
    void wavWriterShouldWritePcm16MonoHeader();
    void recorderAutoShouldPickFirstAvailableBackend();
    void recorderPulseaudioShouldUseParecOrParecord();
    void recorderExplicitUnavailableBackendShouldFail();
    void recorderUnsupportedBackendShouldFail();
    void recorderAutoWithoutToolsShouldReportSupportedBackends();
    void vadEndpointShouldAutostopAfterSpeechThenSilence();
    void vadEndpointShouldNotAutostopWithoutSpeech();
    void vadEndpointShouldIgnoreShortSpeechBursts();
};

namespace {

QString createExecutable(QTemporaryDir *dir, const QString &name, const QByteArray &content)
{
    const QString path = dir->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(content);
    file.close();
    QFile::setPermissions(path,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                    QFile::ReadGroup | QFile::ExeGroup |
                    QFile::ReadOther | QFile::ExeOther);
    return path;
}

QString createExecutable(QTemporaryDir *dir, const QString &name)
{
    return createExecutable(dir, name, "#!/bin/sh\nexit 0\n");
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

class FakeVadFrameClassifier final : public VadFrameClassifier {
public:
    bool initialize(int aggressiveness, QString *errorMessage) override
    {
        Q_UNUSED(errorMessage);
        return aggressiveness >= 0 && aggressiveness <= 3;
    }

    std::optional<bool> processFrame(
            const int16_t *samples,
            std::size_t sampleCount,
            QString *errorMessage) override
    {
        Q_UNUSED(errorMessage);
        if (samples == nullptr || sampleCount == 0) {
            return std::nullopt;
        }
        return samples[0] != 0;
    }
};

QByteArray vadFrame(bool speech)
{
    QByteArray frame(320 * static_cast<int>(sizeof(int16_t)), '\0');
    if (!speech) {
        return frame;
    }

    qToLittleEndian<int16_t>(1000, reinterpret_cast<uchar *>(frame.data()));
    return frame;
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

void CoreBehaviorTest::settingsShouldLoadSeparateHotkeyDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.hotkeyDictate, QStringLiteral("Super+Space"));
    QCOMPARE(settings.hotkeyTranslateEn, QStringLiteral("Super+Shift+Space"));
    QCOMPARE(settings.translateToEn, false);

    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    QCOMPARE(storedSettings.value(QStringLiteral("hotkey_dictate")).toString(), QStringLiteral("Super+Space"));
    QCOMPARE(storedSettings.value(QStringLiteral("hotkey_translate_en")).toString(),
            QStringLiteral("Super+Shift+Space"));
    QVERIFY(!storedSettings.contains(QStringLiteral("translate-to-en")));
}

void CoreBehaviorTest::settingsShouldMigrateLegacyHotkeyToDictationHotkey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("hotkey"), QStringLiteral("Alt+Space"));
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.hotkeyDictate, QStringLiteral("Alt+Space"));
    QCOMPARE(settings.hotkeyTranslateEn, QStringLiteral("Super+Shift+Space"));
    QSettings reloadedSettings(settingsPath, QSettings::IniFormat);
    QCOMPARE(reloadedSettings.value(QStringLiteral("hotkey_dictate")).toString(), QStringLiteral("Alt+Space"));
}

void CoreBehaviorTest::settingsShouldIgnoreTranslateToEnglishAsSavedMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("translate-to-en"), QStringLiteral("true"));
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.translateToEn, false);
}

void CoreBehaviorTest::settingsShouldLoadWakeWordDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.wakeWordEnabled, false);
    QCOMPARE(settings.wakeWordPhrase, QStringLiteral("alexa"));
    QCOMPARE(settings.wakeWordModelPath, QString());
    QCOMPARE(settings.wakeWordThreshold, 0.5);
    QVERIFY(settings.wakeWordSidecarExecutable.endsWith(QStringLiteral("/python/.venv/bin/python")));
    QVERIFY(settings.wakeWordSidecarScript.endsWith(QStringLiteral("/python/openwakeword_sidecar.py")));
}

void CoreBehaviorTest::settingsShouldLoadWakeWordOverrides()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("wake_word_enabled"), true);
    storedSettings.setValue(QStringLiteral("wake_word_phrase"), QStringLiteral("alexa"));
    storedSettings.setValue(QStringLiteral("wake_word_model_path"), QStringLiteral("~/models/alexa_v0.1.onnx"));
    storedSettings.setValue(QStringLiteral("wake_word_threshold"), 0.65);
    storedSettings.setValue(QStringLiteral("wake_word_sidecar_executable"), QStringLiteral("/usr/bin/python3"));
    storedSettings.setValue(QStringLiteral("wake_word_sidecar_script"), QStringLiteral("/opt/oww_sidecar.py"));
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.wakeWordEnabled, true);
    QCOMPARE(settings.wakeWordPhrase, QStringLiteral("alexa"));
    QCOMPARE(settings.wakeWordModelPath, QDir::home().filePath(QStringLiteral("models/alexa_v0.1.onnx")));
    QCOMPARE(settings.wakeWordThreshold, 0.65);
    QCOMPARE(settings.wakeWordSidecarExecutable, QStringLiteral("/usr/bin/python3"));
    QCOMPARE(settings.wakeWordSidecarScript, QStringLiteral("/opt/oww_sidecar.py"));
}

void CoreBehaviorTest::settingsShouldSaveWakeWordEnabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QString errorMessage;
    QVERIFY2(AppSettings::saveWakeWordEnabled(settingsPath, true, &errorMessage), qPrintable(errorMessage));

    QSettings settings(settingsPath, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("wake_word_enabled")).toBool(), true);
}

void CoreBehaviorTest::settingsShouldLoadVadDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.vadAutostopEnabled, false);
    QCOMPARE(settings.vadAggressiveness, 2);
    QCOMPARE(settings.vadEndSilenceMs, 900);
    QCOMPARE(settings.vadMinSpeechMs, 250);
}

void CoreBehaviorTest::settingsShouldLoadVadOverrides()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("vad_autostop_enabled"), true);
    storedSettings.setValue(QStringLiteral("vad_aggressiveness"), 3);
    storedSettings.setValue(QStringLiteral("vad_end_silence_ms"), 700);
    storedSettings.setValue(QStringLiteral("vad_min_speech_ms"), 180);
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.vadAutostopEnabled, true);
    QCOMPARE(settings.vadAggressiveness, 3);
    QCOMPARE(settings.vadEndSilenceMs, 700);
    QCOMPARE(settings.vadMinSpeechMs, 180);
}

void CoreBehaviorTest::settingsShouldSaveVadAutostopEnabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QString errorMessage;
    QVERIFY2(AppSettings::saveVadAutostopEnabled(settingsPath, true, &errorMessage), qPrintable(errorMessage));

    QSettings settings(settingsPath, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("vad_autostop_enabled")).toBool(), true);
}

void CoreBehaviorTest::settingsShouldMigrateLegacyConfigWithoutVadKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("hotkey_dictate"), QStringLiteral("Alt+Space"));
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QCOMPARE(settings.vadAutostopEnabled, false);
    QCOMPARE(settings.vadAggressiveness, 2);
    QCOMPARE(settings.vadEndSilenceMs, 900);
    QCOMPARE(settings.vadMinSpeechMs, 250);

    QSettings migratedSettings(settingsPath, QSettings::IniFormat);
    QVERIFY(migratedSettings.contains(QStringLiteral("vad_autostop_enabled")));
    QVERIFY(migratedSettings.contains(QStringLiteral("vad_aggressiveness")));
    QVERIFY(migratedSettings.contains(QStringLiteral("vad_end_silence_ms")));
    QVERIFY(migratedSettings.contains(QStringLiteral("vad_min_speech_ms")));
}

void CoreBehaviorTest::settingsShouldMigrateLegacyWakeWordRuntimeDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("wake_word_sidecar_executable"), QStringLiteral("python3"));
    storedSettings.setValue(QStringLiteral("wake_word_sidecar_script"), QStringLiteral("openwakeword_sidecar.py"));
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QVERIFY(settings.wakeWordSidecarExecutable.endsWith(QStringLiteral("/python/.venv/bin/python")));
    QVERIFY(settings.wakeWordSidecarScript.endsWith(QStringLiteral("/python/openwakeword_sidecar.py")));
}

void CoreBehaviorTest::settingsShouldMigrateAppDataWakeWordRuntimeDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    const QString legacyRuntimeRoot = QDir::home().filePath(QStringLiteral(".local/share/speak-to-computer/speak-to-computer/python"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("wake_word_sidecar_executable"),
            QDir(legacyRuntimeRoot).filePath(QStringLiteral(".venv/bin/python")));
    storedSettings.setValue(QStringLiteral("wake_word_sidecar_script"),
            QDir(legacyRuntimeRoot).filePath(QStringLiteral("openwakeword_sidecar.py")));
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);

    QVERIFY(settings.wakeWordSidecarExecutable.endsWith(QStringLiteral("/python/.venv/bin/python")));
    QVERIFY(!settings.wakeWordSidecarExecutable.contains(QStringLiteral("/speak-to-computer/speak-to-computer/python")));
    QVERIFY(settings.wakeWordSidecarScript.endsWith(QStringLiteral("/python/openwakeword_sidecar.py")));
    QVERIFY(!settings.wakeWordSidecarScript.contains(QStringLiteral("/speak-to-computer/speak-to-computer/python")));
}

void CoreBehaviorTest::cleanupShouldTrimAndJoinTranscriptLines()
{
    const QString raw = QStringLiteral("  To jest test.  \n\n  Druga linia. \r\n");

    QCOMPARE(TranscriptCleaner::cleanup(raw), QStringLiteral("To jest test. Druga linia."));
}

void CoreBehaviorTest::cleanupShouldDropWhisperNonSpeechAnnotations()
{
    QCOMPARE(TranscriptCleaner::cleanup(QStringLiteral(" [Music]\n")), QString());
    QCOMPARE(TranscriptCleaner::cleanup(QStringLiteral("Pierwsze zdanie.\n[Music]\nDrugie zdanie.")),
            QStringLiteral("Pierwsze zdanie. Drugie zdanie."));
    QCOMPARE(TranscriptCleaner::cleanup(QStringLiteral("Hello [Laughter] world [BLANK_AUDIO]")),
            QStringLiteral("Hello world"));
    QCOMPARE(TranscriptCleaner::cleanup(QStringLiteral("Use [value] literally.")),
            QStringLiteral("Use [value] literally."));
}

void CoreBehaviorTest::whisperRunnerShouldPassTranslateFlagWhenEnabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString argsPath = dir.filePath(QStringLiteral("whisper-args.txt"));
    qputenv("WHISPER_TEST_ARGS", argsPath.toUtf8());

    const QByteArray script =
            "#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > \"$WHISPER_TEST_ARGS\"\n"
            "printf '  translated text.  \\n\\n'\n"
            "exit 0\n";
    const QString whisperCli = createExecutable(&dir, QStringLiteral("whisper-cli"), script);
    QVERIFY(!whisperCli.isEmpty());

    AppSettings settings;
    settings.whisperCli = whisperCli;
    settings.model = createFile(&dir, QStringLiteral("ggml-small.bin"));
    settings.language = QStringLiteral("pl");
    settings.threads = 2;
    settings.translateToEn = true;
    const QString wavPath = createFile(&dir, QStringLiteral("recording.wav"));

    WhisperRunner runner;
    QSignalSpy readySpy(&runner, &WhisperRunner::transcriptionReady);
    QSignalSpy failedSpy(&runner, &WhisperRunner::failed);
    runner.transcribe(wavPath, settings);

    QVERIFY2(readySpy.wait(5000),
            qPrintable(failedSpy.isEmpty() ? QStringLiteral("Timed out waiting for whisper output")
                                           : failedSpy.first().at(0).toString()));
    QCOMPARE(readySpy.first().at(0).toString(), QStringLiteral("translated text."));

    QFile argsFile(argsPath);
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QStringList args = QString::fromUtf8(argsFile.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY2(args.contains(QStringLiteral("-tr")), qPrintable(args.join(QLatin1Char(' '))));
}

void CoreBehaviorTest::whisperRunnerShouldOmitTranslateFlagWhenDisabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString argsPath = dir.filePath(QStringLiteral("whisper-args.txt"));
    qputenv("WHISPER_TEST_ARGS", argsPath.toUtf8());

    const QByteArray script =
            "#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > \"$WHISPER_TEST_ARGS\"\n"
            "printf '  source text.  \\n\\n'\n"
            "exit 0\n";
    const QString whisperCli = createExecutable(&dir, QStringLiteral("whisper-cli"), script);
    QVERIFY(!whisperCli.isEmpty());

    AppSettings settings;
    settings.whisperCli = whisperCli;
    settings.model = createFile(&dir, QStringLiteral("ggml-small.bin"));
    settings.language = QStringLiteral("pl");
    settings.threads = 2;
    settings.translateToEn = false;
    const QString wavPath = createFile(&dir, QStringLiteral("recording.wav"));

    WhisperRunner runner;
    QSignalSpy readySpy(&runner, &WhisperRunner::transcriptionReady);
    QSignalSpy failedSpy(&runner, &WhisperRunner::failed);
    runner.transcribe(wavPath, settings);

    QVERIFY2(readySpy.wait(5000),
            qPrintable(failedSpy.isEmpty() ? QStringLiteral("Timed out waiting for whisper output")
                                           : failedSpy.first().at(0).toString()));
    QCOMPARE(readySpy.first().at(0).toString(), QStringLiteral("source text."));

    QFile argsFile(argsPath);
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QStringList args = QString::fromUtf8(argsFile.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY2(!args.contains(QStringLiteral("-tr")), qPrintable(args.join(QLatin1Char(' '))));
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

void CoreBehaviorTest::vadEndpointShouldAutostopAfterSpeechThenSilence()
{
    VadEndpointDetector detector(std::make_unique<FakeVadFrameClassifier>());
    QString errorMessage;
    const VadEndpointConfig config{2, 200, 100};
    QVERIFY2(detector.reset(config, &errorMessage), qPrintable(errorMessage));

    for (int frameIndex = 0; frameIndex < 5; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(true), &errorMessage));
    }
    QVERIFY(!detector.shouldAutoStop());

    for (int frameIndex = 0; frameIndex < 9; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(false), &errorMessage));
    }
    QVERIFY(!detector.shouldAutoStop());

    QVERIFY(detector.consumePcmChunk(vadFrame(false), &errorMessage));
    QVERIFY(detector.shouldAutoStop());
}

void CoreBehaviorTest::vadEndpointShouldNotAutostopWithoutSpeech()
{
    VadEndpointDetector detector(std::make_unique<FakeVadFrameClassifier>());
    QString errorMessage;
    const VadEndpointConfig config{2, 200, 100};
    QVERIFY2(detector.reset(config, &errorMessage), qPrintable(errorMessage));

    for (int frameIndex = 0; frameIndex < 20; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(false), &errorMessage));
    }
    QVERIFY(!detector.shouldAutoStop());
}

void CoreBehaviorTest::vadEndpointShouldIgnoreShortSpeechBursts()
{
    VadEndpointDetector detector(std::make_unique<FakeVadFrameClassifier>());
    QString errorMessage;
    const VadEndpointConfig config{2, 200, 120};
    QVERIFY2(detector.reset(config, &errorMessage), qPrintable(errorMessage));

    for (int frameIndex = 0; frameIndex < 5; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(true), &errorMessage));
    }
    for (int frameIndex = 0; frameIndex < 12; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(false), &errorMessage));
    }

    QVERIFY(!detector.shouldAutoStop());
}

QTEST_MAIN(CoreBehaviorTest)

#include "CoreBehaviorTest.moc"
