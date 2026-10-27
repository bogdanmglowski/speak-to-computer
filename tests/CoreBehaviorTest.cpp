#include "AppSettings.h"
#include "AudioRecorder.h"
#include "BuzzAsrRunner.h"
#include "ClipboardPaster.h"
#include "OverlayWidget.h"
#include "PreferencesDialog.h"
#include "TranscriptCleaner.h"
#include "VadEndpointDetector.h"
#include "WavWriter.h"
#include "WhisperRunner.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QSettings>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QIcon>
#include <QTemporaryDir>
#include <QtTest>
#include <QtEndian>

#include <memory>

class CoreBehaviorTest : public QObject {
    Q_OBJECT

private slots:
    void settingsShouldExpandHomePath();
    void settingsShouldMigrateTemporaryBuzzAsrRuntime();
    void buzzAsrShouldPreserveUnicodePathsWithoutUnsupportedPrompts();
    void buzzAsrShouldReportStartupErrorBeforeProcessExit();
    void clipboardCopyShouldNotSendKeystrokes();
    void herdrGpuiShouldPasteWithCtrlShiftV();
    void focusPasteShouldReadActiveWindowClass();
    void emptyResultAlertShouldHideItself();
    void iconShouldRenderAtTraySize();
    void vadEndpointShouldAllowThinkingPause();
    void settingsShouldResolveModelLabel();
    void settingsShouldReturnOnlyExistingModelPaths();
    void settingsShouldSaveModelPath();
    void settingsShouldSaveAndReloadAllEditableFields();
    void settingsShouldDefaultPasteTargetToPin();
    void settingsShouldSaveAndReloadMiniOverlayToggle();
    void preferencesShouldReturnMiniOverlayToggle();
    void settingsShouldLoadSeparateHotkeyDefaults();
    void settingsShouldMigrateLegacyHotkeyToDictationHotkey();
    void settingsShouldIgnoreTranslateToEnglishAsSavedMode();
    void settingsShouldLoadWakeWordDefaults();
    void settingsShouldLoadWakeWordOverrides();
    void settingsShouldSaveWakeWordEnabled();
    void settingsShouldLoadVadDefaults();
    void settingsShouldLoadVadOverrides();
    void settingsShouldSaveVadAutostopEnabled();
    void settingsShouldSaveAndReloadVadEndSilenceMs();
    void settingsShouldFallbackToDefaultVadEndSilenceMsWhenInvalid();
    void settingsShouldMigrateLegacyConfigWithoutVadKeys();
    void settingsShouldMigrateLegacyWakeWordRuntimeDefaults();
    void settingsShouldMigrateAppDataWakeWordRuntimeDefaults();
    void overlayShouldRequestCloseOnShiftEscape();
    void overlayShouldIgnoreBareEscape();
    void overlayShouldShowVoiceAmplitudeAboveTheMeterLine();
    void overlayShouldResetVoiceAmplitudeForNewRecording();
    void overlayShouldReturnToFlatWaveformDuringSilence();
    void overlayShouldKeepVoiceShapeStationary();
    void overlayMiniShouldHaveNoPanelBackground();
    void overlayMiniShouldKeepErrorsAndTranscriptionInFullView();
    void overlayMiniShouldNotActivateInvisibleControls();
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

void CoreBehaviorTest::overlayShouldShowVoiceAmplitudeAboveTheMeterLine()
{
    OverlayWidget overlay;
    overlay.showRecording(QStringLiteral("Test"), {});
    const QImage silent = overlay.grab().toImage();
    overlay.setAudioLevel(1.0);
    const QImage loud = overlay.grab().toImage();
    int changedPixelCount = 0;
    for (int yPx = overlay.height() - 48; yPx < overlay.height() - 40; ++yPx) {
        for (int xPx = 64; xPx < 264; ++xPx) {
            if (silent.pixel(xPx, yPx) != loud.pixel(xPx, yPx)) {
                ++changedPixelCount;
            }
        }
    }
    QVERIFY(changedPixelCount > 100);
}

void CoreBehaviorTest::overlayShouldResetVoiceAmplitudeForNewRecording()
{
    OverlayWidget overlay;
    overlay.showRecording(QStringLiteral("Test"), {});
    const QImage silent = overlay.grab().toImage();
    overlay.setAudioLevel(0.8);
    overlay.showTranscribing(QStringLiteral("Test"));
    overlay.showRecording(QStringLiteral("Test"), {});
    QCOMPARE(overlay.grab().toImage(), silent);
}

void CoreBehaviorTest::overlayShouldReturnToFlatWaveformDuringSilence()
{
    OverlayWidget overlay;
    overlay.showRecording(QStringLiteral("Test"), {});
    const QImage silent = overlay.grab().toImage();
    overlay.setAudioLevel(0.8);
    overlay.setAudioLevel(0.0);
    QCOMPARE(overlay.grab().toImage(), silent);
}

void CoreBehaviorTest::overlayShouldKeepVoiceShapeStationary()
{
    OverlayWidget overlay;
    overlay.showRecording(QStringLiteral("Test"), {});
    overlay.setAudioLevel(0.8);
    const QImage firstPulse = overlay.grab().toImage();
    overlay.setAudioLevel(0.2);
    overlay.setAudioLevel(0.8);
    QCOMPARE(overlay.grab().toImage(), firstPulse);
}

void CoreBehaviorTest::settingsShouldSaveAndReloadMiniOverlayToggle()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    AppSettings settings = AppSettings::loadFromPath(path);
    QVERIFY(!settings.miniOverlayEnabled);
    settings.miniOverlayEnabled = true;
    QString errorMessage;
    QVERIFY2(AppSettings::save(settings, &errorMessage), qPrintable(errorMessage));
    QVERIFY(AppSettings::loadFromPath(path).miniOverlayEnabled);
    QVERIFY2(AppSettings::saveMiniOverlayEnabled(path, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY(!AppSettings::loadFromPath(path).miniOverlayEnabled);
}

void CoreBehaviorTest::preferencesShouldReturnMiniOverlayToggle()
{
    AppSettings settings;
    PreferencesDialog dialog(settings);
    QCheckBox *miniCheck = nullptr;
    for (QCheckBox *check : dialog.findChildren<QCheckBox *>()) {
        if (check->text() == QStringLiteral("Tryb mini")) {
            miniCheck = check;
        }
    }
    QVERIFY(miniCheck != nullptr);
    QVERIFY(!miniCheck->isChecked());
    miniCheck->setChecked(true);
    QVERIFY(dialog.editedSettings().miniOverlayEnabled);
    miniCheck->setChecked(false);
    QVERIFY(!dialog.editedSettings().miniOverlayEnabled);
}

void CoreBehaviorTest::overlayMiniShouldHaveNoPanelBackground()
{
    OverlayWidget overlay;
    overlay.setMiniModeEnabled(true);
    overlay.showRecording(QStringLiteral("Original (Polish)"), {QStringLiteral("Hidden hint")});
    overlay.setAudioLevel(0.8);
    overlay.setElapsedMs(12000);
    overlay.setGpuMemoryLabel(QStringLiteral("GPU 6280/8151 MB"));
    const QImage image = overlay.grab().toImage();
    QCOMPARE(overlay.size(), QSize(380, 56));
    QCOMPARE(image.pixelColor(24, 12).alpha(), 0);
}

void CoreBehaviorTest::overlayMiniShouldKeepErrorsAndTranscriptionInFullView()
{
    OverlayWidget overlay;
    overlay.setMiniModeEnabled(true);
    overlay.showRecording(QStringLiteral("Test"), {});
    const QSize miniSize = overlay.size();
    overlay.showTranscribing(QStringLiteral("Test"));
    QVERIFY(overlay.width() > miniSize.width());
    QVERIFY(overlay.height() > miniSize.height());
    overlay.showError(QStringLiteral("CUDA error"));
    QVERIFY(overlay.height() > miniSize.height());
    QVERIFY(overlay.grab().toImage().pixelColor(24, 12).alpha() > 0);
    overlay.showRecording(QStringLiteral("Test"), {});
    QCOMPARE(overlay.size(), miniSize);
    overlay.setMiniModeEnabled(false);
    QVERIFY(overlay.width() > miniSize.width());
}

void CoreBehaviorTest::overlayMiniShouldNotActivateInvisibleControls()
{
    OverlayWidget overlay;
    overlay.setMiniModeEnabled(true);
    overlay.showRecording(QStringLiteral("Test"), {});
    QSignalSpy closed(&overlay, &OverlayWidget::closeRequested);
    QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(overlay.width() - 30, 30));
    QCOMPARE(closed.count(), 0);
    QTest::keyClick(&overlay, Qt::Key_Escape, Qt::ShiftModifier);
    QCOMPARE(closed.count(), 1);
}

void CoreBehaviorTest::settingsShouldMigrateTemporaryBuzzAsrRuntime()
{
    QTemporaryDir dir;
    const QString path = createFile(&dir, QStringLiteral("settings.ini"),
            "[General]\nbuzzasr_python=/tmp/opencode/cer-poc/.venv/bin/python\nbuzzasr_script=/old/source/scripts/buzzasr_transcribe.py\n");
    const AppSettings settings = AppSettings::loadFromPath(path);
    const QDir runtime(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    QCOMPARE(settings.buzzAsrPython, runtime.filePath(QStringLiteral("speak-to-computer/buzzasr/.venv/bin/python")));
    QCOMPARE(settings.buzzAsrScript, runtime.filePath(QStringLiteral("speak-to-computer/buzzasr/buzzasr_transcribe.py")));
}

void CoreBehaviorTest::buzzAsrShouldPreserveUnicodePathsWithoutUnsupportedPrompts()
{
    QTemporaryDir dir;
    const QByteArray previousPath = qgetenv("PATH");
    const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
    const auto restoreEnvironment = qScopeGuard([&]() {
        qputenv("PATH", previousPath);
        qputenv("XDG_RUNTIME_DIR", previousRuntime);
    });
    QVERIFY(!createExecutable(&dir, QStringLiteral("pgrep"), "#!/bin/sh\nexit 1\n").isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + previousPath);
    qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
    AppSettings settings;
    settings.buzzAsrPython = QStringLiteral("/usr/bin/python3");
    settings.dictionaryPath = createFile(&dir, QStringLiteral("dictionary.txt"), "# names\nTaskBot\nPostgreSQL\n");
    settings.buzzAsrScript = createFile(&dir, QStringLiteral("fake daemon.py"), R"PY(
import json, os, time
with open(os.environ["INBOX"], "a+") as inbox:
    inbox.seek(0, 2)
    print(json.dumps({"ready": True}), flush=True)
    while True:
        line = inbox.readline()
        if not line:
            time.sleep(0.01)
            continue
        request = json.loads(line)
        assert set(request) == {"wav_path"}
        print(json.dumps({"text": "Zażółć\n" + request["wav_path"]}), flush=True)
)PY");
    BuzzAsrRunner runner;
    QSignalSpy ready(&runner, &BuzzAsrRunner::transcriptionReady);
    QSignalSpy failed(&runner, &BuzzAsrRunner::failed);
    runner.transcribe(QStringLiteral("/audio/zażółć test.wav"), settings);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("Zażółć /audio/zażółć test.wav"));
}

void CoreBehaviorTest::buzzAsrShouldReportStartupErrorBeforeProcessExit()
{
    QTemporaryDir dir;
    const QByteArray previousPath = qgetenv("PATH");
    const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
    const auto restoreEnvironment = qScopeGuard([&]() {
        qputenv("PATH", previousPath);
        qputenv("XDG_RUNTIME_DIR", previousRuntime);
    });
    QVERIFY(!createExecutable(&dir, QStringLiteral("pgrep"), "#!/bin/sh\nexit 1\n").isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + previousPath);
    qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
    AppSettings settings;
    settings.buzzAsrPython = QStringLiteral("/usr/bin/python3");
    settings.buzzAsrScript = createFile(&dir, QStringLiteral("failed daemon.py"),
            "import json\nprint(json.dumps({'error': 'CUDA out of memory'}), flush=True)\nraise SystemExit(1)\n");
    BuzzAsrRunner runner;
    QSignalSpy failed(&runner, &BuzzAsrRunner::failed);
    runner.transcribe(QStringLiteral("/audio/test.wav"), settings);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("CUDA out of memory")));
}

void CoreBehaviorTest::clipboardCopyShouldNotSendKeystrokes()
{
    QTemporaryDir dir;
    const QByteArray previousPath = qgetenv("PATH");
    const auto restoreEnvironment = qScopeGuard([&]() { qputenv("PATH", previousPath); });
    QVERIFY(!createExecutable(&dir, QStringLiteral("xclip"),
            "#!/bin/sh\ncat > \"$(dirname \"$0\")/copied.txt\"\n").isEmpty());
    QVERIFY(!createExecutable(&dir, QStringLiteral("xdotool"),
            "#!/bin/sh\ntouch \"$(dirname \"$0\")/keystrokes\"\n").isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + previousPath);
    ClipboardPaster paster;
    QString errorMessage;
    QVERIFY2(paster.copy(QStringLiteral("Zażółć gęślą jaźń"), &errorMessage), qPrintable(errorMessage));
    QFile copied(dir.filePath(QStringLiteral("copied.txt")));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(copied.readAll()), QStringLiteral("Zażółć gęślą jaźń"));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("keystrokes"))));
}

void CoreBehaviorTest::herdrGpuiShouldPasteWithCtrlShiftV()
{
    // Herdr GPUI pastes on ctrl+shift+v. ctrl+v is delivered to the pane as a raw key.
    const QString herdrClass = QStringLiteral(
            "WM_CLASS(STRING) = \"so.pen.herdr-gpui\", \"so.pen.herdr-gpui\"");
    QCOMPARE(ClipboardPaster::pasteKeystrokeForWindowClass(herdrClass), QStringLiteral("ctrl+shift+v"));
    QCOMPARE(ClipboardPaster::pasteKeystrokeForWindowClass(QStringLiteral("firefox")),
            QStringLiteral("ctrl+v"));
}

void CoreBehaviorTest::focusPasteShouldReadActiveWindowClass()
{
    // Window 0 is focus mode. The shortcut must follow the focused window,
    // not fall back to ctrl+v before anyone looks at WM_CLASS.
    QTemporaryDir dir;
    const QByteArray previousPath = qgetenv("PATH");
    const auto restoreEnvironment = qScopeGuard([&]() { qputenv("PATH", previousPath); });
    QVERIFY(!createExecutable(&dir, QStringLiteral("xdotool"),
            "#!/bin/sh\n"
            "if [ \"$1\" = getactivewindow ]; then echo 4242; exit 0; fi\n"
            "exit 1\n")
                    .isEmpty());
    QVERIFY(!createExecutable(&dir, QStringLiteral("xprop"),
            "#!/bin/sh\n"
            "echo 'WM_CLASS(STRING) = \"so.pen.herdr-gpui\", \"so.pen.herdr-gpui\"'\n")
                    .isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + previousPath);

    QCOMPARE(ClipboardPaster::pasteKeystrokeFor(0), QStringLiteral("ctrl+shift+v"));
}

void CoreBehaviorTest::emptyResultAlertShouldHideItself()
{
    // No transcript is not a fault the user has to dismiss. The window closes itself.
    OverlayWidget overlay;
    overlay.showError(QStringLiteral("Whisper did not return any text."),
            QStringLiteral("Dictation alert"), 40);
    QVERIFY(overlay.isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!overlay.isVisible(), 1000);
}

void CoreBehaviorTest::iconShouldRenderAtTraySize()
{
    const QIcon icon(QStringLiteral(":/icons/speak-to-computer.svg"));
    QVERIFY(!icon.pixmap(16, 16).isNull());
}

void CoreBehaviorTest::vadEndpointShouldAllowThinkingPause()
{
    VadEndpointDetector detector(std::make_unique<FakeVadFrameClassifier>());
    QString errorMessage;
    QVERIFY(detector.reset({2, 1500, 100}, &errorMessage));
    for (int frameIndex = 0; frameIndex < 5; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(true), &errorMessage));
    }
    for (int frameIndex = 0; frameIndex < 70; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(false), &errorMessage));
    }
    QVERIFY(!detector.shouldAutoStop());
    for (int frameIndex = 0; frameIndex < 5; ++frameIndex) {
        QVERIFY(detector.consumePcmChunk(vadFrame(false), &errorMessage));
    }
    QVERIFY(detector.shouldAutoStop());
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

void CoreBehaviorTest::settingsShouldSaveAndReloadAllEditableFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));

    AppSettings expected;
    expected.settingsPath = settingsPath;
    expected.hotkeyDictate = QStringLiteral("Ctrl+Alt+D");
    expected.hotkeyTranslateEn = QStringLiteral("Ctrl+Alt+E");
    expected.audioBackend = QStringLiteral("pipewire");
    expected.language = QStringLiteral("en");
    expected.whisperCli = QStringLiteral("/opt/whisper-cli");
    expected.model = QStringLiteral("/models/ggml-base.bin");
    expected.activationSound = QStringLiteral("/sounds/start.wav");
    expected.endSound = QStringLiteral("/sounds/end.wav");
    expected.wakeWordEnabled = true;
    expected.wakeWordPhrase = QStringLiteral("computer");
    expected.wakeWordModelPath = QStringLiteral("/models/computer.onnx");
    expected.wakeWordThreshold = 0.65;
    expected.wakeWordSidecarExecutable = QStringLiteral("/venv/bin/python");
    expected.wakeWordSidecarScript = QStringLiteral("/app/openwakeword_sidecar.py");
    expected.pasteTarget = QString::fromUtf8(AppSettings::kPasteTargetFocus);
    expected.vadAutostopEnabled = true;
    expected.vadAggressiveness = 3;
    expected.vadEndSilenceMs = 700;
    expected.vadMinSpeechMs = 180;
    expected.threads = 6;
    expected.translateToEn = true;

    QString errorMessage;
    QVERIFY2(AppSettings::save(expected, &errorMessage), qPrintable(errorMessage));

    const AppSettings actual = AppSettings::loadFromPath(settingsPath);
    QCOMPARE(actual.settingsPath, settingsPath);
    QCOMPARE(actual.hotkeyDictate, expected.hotkeyDictate);
    QCOMPARE(actual.hotkeyTranslateEn, expected.hotkeyTranslateEn);
    QCOMPARE(actual.audioBackend, expected.audioBackend);
    QCOMPARE(actual.language, expected.language);
    QCOMPARE(actual.pasteTarget, expected.pasteTarget);
    QCOMPARE(actual.whisperCli, expected.whisperCli);
    QCOMPARE(actual.model, expected.model);
    QCOMPARE(actual.activationSound, expected.activationSound);
    QCOMPARE(actual.endSound, expected.endSound);
    QCOMPARE(actual.wakeWordEnabled, expected.wakeWordEnabled);
    QCOMPARE(actual.wakeWordPhrase, expected.wakeWordPhrase);
    QCOMPARE(actual.wakeWordModelPath, expected.wakeWordModelPath);
    QCOMPARE(actual.wakeWordThreshold, expected.wakeWordThreshold);
    QCOMPARE(actual.wakeWordSidecarExecutable, expected.wakeWordSidecarExecutable);
    QCOMPARE(actual.wakeWordSidecarScript, expected.wakeWordSidecarScript);
    QCOMPARE(actual.vadAutostopEnabled, expected.vadAutostopEnabled);
    QCOMPARE(actual.vadAggressiveness, expected.vadAggressiveness);
    QCOMPARE(actual.vadEndSilenceMs, expected.vadEndSilenceMs);
    QCOMPARE(actual.vadMinSpeechMs, expected.vadMinSpeechMs);
    QCOMPARE(actual.threads, expected.threads);
    QCOMPARE(actual.translateToEn, false);
}

void CoreBehaviorTest::settingsShouldDefaultPasteTargetToPin()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    const AppSettings settings = AppSettings::loadFromPath(settingsPath);
    QCOMPARE(settings.pasteTarget, QString::fromUtf8(AppSettings::kPasteTargetPin));
    QVERIFY(settings.isPinPasteTarget());
    QVERIFY(!settings.isFocusPasteTarget());
    QCOMPARE(AppSettings::pasteTargetLabel(QStringLiteral("focus")), QStringLiteral("Focus"));
    QCOMPARE(AppSettings::normalizePasteTarget(QStringLiteral("FOCUS")),
            QString::fromUtf8(AppSettings::kPasteTargetFocus));
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

void CoreBehaviorTest::settingsShouldSaveAndReloadVadEndSilenceMs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QString errorMessage;
    QVERIFY2(AppSettings::saveVadEndSilenceMs(settingsPath, 500, &errorMessage), qPrintable(errorMessage));

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);
    QCOMPARE(settings.vadEndSilenceMs, 500);

    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    QCOMPARE(storedSettings.value(QStringLiteral("vad_end_silence_ms")).toInt(), 500);
}

void CoreBehaviorTest::settingsShouldFallbackToDefaultVadEndSilenceMsWhenInvalid()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings storedSettings(settingsPath, QSettings::IniFormat);
    storedSettings.setValue(QStringLiteral("vad_end_silence_ms"), 0);
    storedSettings.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);
    QCOMPARE(settings.vadEndSilenceMs, 900);
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

void CoreBehaviorTest::overlayShouldRequestCloseOnShiftEscape()
{
    OverlayWidget overlay;
    QSignalSpy closeSpy(&overlay, &OverlayWidget::closeRequested);

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::ShiftModifier);
    QVERIFY(QCoreApplication::sendEvent(&overlay, &event));

    QCOMPARE(closeSpy.count(), 1);
}

void CoreBehaviorTest::overlayShouldIgnoreBareEscape()
{
    OverlayWidget overlay;
    QSignalSpy closeSpy(&overlay, &OverlayWidget::closeRequested);

    QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QVERIFY(QCoreApplication::sendEvent(&overlay, &event));

    QCOMPARE(closeSpy.count(), 0);
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

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }

    QApplication app(argc, argv);
    CoreBehaviorTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "CoreBehaviorTest.moc"
