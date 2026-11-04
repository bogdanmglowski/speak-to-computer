#include "AppSettings.h"
#include "AudioRecorder.h"
#include "BuzzAsrRunner.h"
#include "ClipboardPaster.h"
#include "MeetingDialog.h"
#include "MeetingAnalysisDialog.h"
#include "MeetingStore.h"
#include "OverlayWidget.h"
#include "ParakeetRunner.h"
#include "PreferencesDialog.h"
#include "TranscriptCleaner.h"
#include "VadEndpointDetector.h"
#include "WavWriter.h"
#include "WhisperRunner.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QInputDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
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
    void settingsShouldSaveParakeetAndPushToTalk();
    void settingsShouldRejectReservedPushToTalkButtons();
    void parakeetRunnerShouldHandleUnicodeAndErrors();
    void parakeetRunnerShouldExcludeNxPreload();
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
    void settingsShouldMigrateLegacyOllamaAddress();
    void huggingFaceSetupShouldInstallAndSaveSettings();
    void ollamaPreferencesShouldAcceptNetworkServer();
    void ollamaPreferencesShouldManageMultipleServers();
    void meetingStorageShouldKeepEntriesInConfiguredDirectory();
    void meetingTableShouldWrapStatusTextWithoutElision();
    void meetingRecordingAndTranscriptionShouldComplete();
    void meetingSpeakerTranscriptionShouldSaveAndRename();
    void meetingAnalysisShouldSaveVersionsAndDetectChanges();
    void meetingAudioFilterShouldPreserveSeparateChannels();
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
    void whisperRunnerShouldStreamMeetingProgress();
    void overlayExpandedShouldSeparateHeaderFromControls();
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

void CoreBehaviorTest::settingsShouldSaveParakeetAndPushToTalk()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    AppSettings settings = AppSettings::loadFromPath(path);
    QCOMPARE(settings.pushToTalkButton, 11);
    QVERIFY(!settings.pushToTalkSendEnter);
    settings.engine = QString::fromUtf8(AppSettings::kEngineParakeet);
    settings.pushToTalkButton = 12;
    settings.pushToTalkSendEnter = true;
    QString errorMessage;
    QVERIFY2(AppSettings::save(settings, &errorMessage), qPrintable(errorMessage));
    const AppSettings loaded = AppSettings::loadFromPath(path);
    QVERIFY(loaded.isParakeetEngine());
    QVERIFY(!loaded.isWhisperCppEngine());
    QCOMPARE(loaded.pushToTalkButton, 12);
    QVERIFY(loaded.pushToTalkSendEnter);
}

void CoreBehaviorTest::settingsShouldRejectReservedPushToTalkButtons()
{
    QTemporaryDir dir;
    AppSettings settings = AppSettings::loadFromPath(dir.filePath(QStringLiteral("settings.ini")));
    QString errorMessage;
    for (int button : {1, 7, 9, 10, 256}) {
        settings.pushToTalkButton = button;
        QVERIFY(!AppSettings::save(settings, &errorMessage));
        QVERIFY(!errorMessage.isEmpty());
    }
    settings.pushToTalkButton = 0;
    QVERIFY2(AppSettings::save(settings, &errorMessage), qPrintable(errorMessage));
}

void CoreBehaviorTest::parakeetRunnerShouldHandleUnicodeAndErrors()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.parakeetPython = QStringLiteral("/usr/bin/python3");
    settings.parakeetScript = createFile(&dir, QStringLiteral("fake parakeet.py"), R"PY(
import json, os, time
with open(os.environ['INBOX'], 'a+', encoding='utf-8') as inbox:
    inbox.seek(0, 2)
    print(json.dumps({'ready': True}), flush=True)
    while True:
        line = inbox.readline()
        if not line:
            time.sleep(0.01)
            continue
        request = json.loads(line)
        if request['wav_path'] == 'missing':
            print(json.dumps({'error': 'missing audio'}), flush=True)
        else:
            print(json.dumps({'text': 'Zażółć ' + request['wav_path']}), flush=True)
)PY");
    ParakeetRunner runner;
    QSignalSpy ready(&runner, &ParakeetRunner::transcriptionReady);
    QSignalSpy failed(&runner, &ParakeetRunner::failed);
    runner.transcribe(QStringLiteral("/audio/gęślą.wav"), settings);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("Zażółć /audio/gęślą.wav"));
    runner.transcribe(QStringLiteral("missing"), settings);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("missing audio")));
}

void CoreBehaviorTest::parakeetRunnerShouldExcludeNxPreload()
{
    QTemporaryDir dir;
    const QByteArray previousPreload = qgetenv("LD_PRELOAD");
    const bool hadPreload = qEnvironmentVariableIsSet("LD_PRELOAD");
    const auto restoreEnvironment = qScopeGuard([&]() {
        if (hadPreload) {
            qputenv("LD_PRELOAD", previousPreload);
        } else {
            qunsetenv("LD_PRELOAD");
        }
    });
    qputenv("LD_PRELOAD", "/usr/NX/lib/libnxegl.so:libc.so.6");
    AppSettings settings;
    settings.parakeetPython = QStringLiteral("/usr/bin/python3");
    settings.parakeetScript = createFile(&dir, QStringLiteral("preload.py"), R"PY(
import json, os, time
with open(os.environ['INBOX'], 'a+', encoding='utf-8') as inbox:
    inbox.seek(0, 2)
    print(json.dumps({'ready': True}), flush=True)
    while not inbox.readline():
        time.sleep(0.01)
    print(json.dumps({'text': os.environ.get('LD_PRELOAD', '')}), flush=True)
)PY");
    ParakeetRunner runner;
    QSignalSpy ready(&runner, &ParakeetRunner::transcriptionReady);
    runner.transcribe(QStringLiteral("/audio/test.wav"), settings);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("libc.so.6"));
}

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

void CoreBehaviorTest::overlayExpandedShouldSeparateHeaderFromControls()
{
    OverlayWidget overlay;
    overlay.showRecording(QStringLiteral("Recording"), {});

    QCOMPARE(overlay.width(), 600);
    QCOMPARE(overlay.height(), 202);
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
    expected.meetingsDirectory = dir.filePath(QStringLiteral("meeting archive"));

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
    QCOMPARE(actual.meetingsDirectory, expected.meetingsDirectory);
}

void CoreBehaviorTest::settingsShouldMigrateLegacyOllamaAddress()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    QSettings legacy(settingsPath, QSettings::IniFormat);
    legacy.setValue(QStringLiteral("analysis_ollama_url"), QStringLiteral("http://karaczan:11434"));
    legacy.sync();

    const AppSettings settings = AppSettings::loadFromPath(settingsPath);
    QCOMPARE(settings.analysisOllamaUrls,
            QStringList({QStringLiteral("http://karaczan:11434"), QStringLiteral("http://127.0.0.1:11434")}));
}

void CoreBehaviorTest::meetingStorageShouldKeepEntriesInConfiguredDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const AppSettings defaults = AppSettings::loadFromPath(dir.filePath(QStringLiteral("defaults.ini")));
    QVERIFY(defaults.meetingsDirectory.endsWith(QStringLiteral("/speak-to-computer/meetings")));
    const QString root = dir.filePath(QStringLiteral("archive"));
    MeetingEntry first, second;
    QString error;
    QVERIFY2(MeetingStore::create(root, &first, &error), qPrintable(error));
    QVERIFY2(MeetingStore::create(root, &second, &error), qPrintable(error));
    QVERIFY(first.directory != second.directory);
    first.name = QStringLiteral("Review / planning");
    first.durationSeconds = 3600;
    first.status = QStringLiteral("Ready");
    QVERIFY2(MeetingStore::save(first, &error), qPrintable(error));
    const auto entries = MeetingStore::list(root);
    QCOMPARE(entries.size(), 2);
    bool found = false;
    for (const auto &entry : entries) {
        if (entry.directory == first.directory) {
            QCOMPARE(entry.name, first.name);
            QCOMPARE(entry.durationSeconds, 3600);
            found = true;
        }
    }
    QVERIFY(found);
    QVERIFY(MeetingStore::list(dir.filePath(QStringLiteral("other-root"))).isEmpty());
    QVERIFY(QFileInfo::exists(first.directory));
}

void CoreBehaviorTest::huggingFaceSetupShouldInstallAndSaveSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    struct RestoreEnvironment {
        QByteArray path = qgetenv("PATH");
        QByteArray home = qgetenv("TEST_HF_HOME"); bool hadHome = qEnvironmentVariableIsSet("TEST_HF_HOME");
        QByteArray fail = qgetenv("TEST_HF_FAIL"); bool hadFail = qEnvironmentVariableIsSet("TEST_HF_FAIL");
        ~RestoreEnvironment() {
            qputenv("PATH", path);
            if (hadHome) qputenv("TEST_HF_HOME", home); else qunsetenv("TEST_HF_HOME");
            if (hadFail) qputenv("TEST_HF_FAIL", fail); else qunsetenv("TEST_HF_FAIL");
        }
    } restore;
    const QByteArray fakePython =
            "#!/bin/sh\n"
            "if [ \"$1\" = -m ] && [ \"$2\" = venv ]; then mkdir -p \"$3/bin\"; cp \"$0\" \"$3/bin/python\"; exit $?; fi\n"
            "if [ \"$1\" = -c ]; then\n"
            "  if [ \"$TEST_HF_FAIL\" = 1 ]; then echo \"$HF_TOKEN\" >&2; exit 1; fi\n"
            "  [ \"$HF_TOKEN\" = hf_test_secret ] && [ \"$HF_HOME\" = \"$TEST_HF_HOME\" ] || exit 1\n"
            "fi\nexit 0\n";
    QVERIFY(!createExecutable(&dir, QStringLiteral("python3"), fakePython).isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + restore.path);
    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    const QString runtimePath = dir.filePath(QStringLiteral("runtime"));
    const QString cachePath = dir.filePath(QStringLiteral("cache"));
    qputenv("TEST_HF_HOME", cachePath.toUtf8());
    AppSettings settings = AppSettings::loadFromPath(settingsPath);
    PreferencesDialog dialog(settings);
    auto *token = dialog.findChild<QLineEdit *>(QStringLiteral("huggingFaceToken"));
    auto *cache = dialog.findChild<QLineEdit *>(QStringLiteral("huggingFaceHome"));
    auto *runtime = dialog.findChild<QLineEdit *>(QStringLiteral("diarizationRuntimeDirectory"));
    auto *setup = dialog.findChild<QPushButton *>(QStringLiteral("diarizationSetup"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("diarizationSetupStatus"));
    QVERIFY(token && cache && runtime && setup && status);
    token->setText(QStringLiteral("hf_test_secret"));
    cache->setText(cachePath); runtime->setText(runtimePath);
    setup->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith(QStringLiteral("Integration ready")), 5000);
    QString error;
    QVERIFY2(AppSettings::save(dialog.editedSettings(), &error), qPrintable(error));
    const AppSettings loaded = AppSettings::loadFromPath(settingsPath);
    QCOMPARE(loaded.huggingFaceToken, QStringLiteral("hf_test_secret"));
    QCOMPARE(loaded.huggingFaceHome, cachePath);
    QCOMPARE(loaded.diarizationRuntimeDirectory, runtimePath);
    QFile tokenFile(dir.filePath(QStringLiteral("huggingface-token")));
    QVERIFY(tokenFile.exists());
    QVERIFY(!(tokenFile.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)));
    QFile ini(settingsPath);
    QVERIFY(ini.open(QIODevice::ReadOnly));
    QVERIFY(!ini.readAll().contains("hf_test_secret"));
    qputenv("TEST_HF_FAIL", "1");
    setup->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith(QStringLiteral("Setup failed")), 5000);
    QVERIFY(!status->text().contains(QStringLiteral("hf_test_secret")));
    token->clear();
    QVERIFY(AppSettings::save(dialog.editedSettings(), &error));
    QVERIFY(!tokenFile.exists());
}

void CoreBehaviorTest::ollamaPreferencesShouldAcceptNetworkServer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    struct RestorePath { QByteArray path = qgetenv("PATH"); ~RestorePath() { qputenv("PATH", path); } } restore;
    const QByteArray fakePython =
            "#!/bin/sh\n"
            "[ \"$2\" = --list-models ] || exit 1\n"
            "case \" $* \" in *' --endpoint http://karaczan:11434 '*) ;; *' --endpoint https://karaczan:11434 '*) ;; *) exit 2 ;; esac\n"
            "printf '%s\\n' '{\"models\":[\"qwen3:8b\"]}'\n";
    QVERIFY(!createExecutable(&dir, QStringLiteral("python3"), fakePython).isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + restore.path);
    AppSettings settings = AppSettings::loadFromPath(dir.filePath(QStringLiteral("settings.ini")));
    PreferencesDialog dialog(settings);
    auto *address = dialog.findChild<QLineEdit *>(QStringLiteral("analysisOllamaUrl"));
    auto *check = dialog.findChild<QPushButton *>(QStringLiteral("analysisCheckOllama"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("analysisOllamaStatus"));
    QVERIFY(address && check && status);
    address->setText(QStringLiteral("http://karaczan:11434"));
    check->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("Connected. Models: qwen3:8b")), 5000);
    QString error;
    QVERIFY2(AppSettings::save(dialog.editedSettings(), &error), qPrintable(error));
    const QStringList savedUrls{QStringLiteral("http://karaczan:11434")};
    QCOMPARE(AppSettings::loadFromPath(settings.settingsPath).analysisOllamaUrls, savedUrls);
    address->setText(QStringLiteral("https://karaczan:11434"));
    check->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("Connected. Models: qwen3:8b")), 5000);
    address->setText(QStringLiteral("http://karaczan/path"));
    check->click();
    QVERIFY(status->text().contains(QStringLiteral("without a path")));
}

void CoreBehaviorTest::ollamaPreferencesShouldManageMultipleServers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    AppSettings settings = AppSettings::loadFromPath(dir.filePath(QStringLiteral("settings.ini")));
    settings.analysisOllamaUrls = {QStringLiteral("http://127.0.0.1:11434"), QStringLiteral("http://karaczan:11434")};
    PreferencesDialog dialog(settings);
    auto *servers = dialog.findChild<QListWidget *>(QStringLiteral("analysisOllamaServers"));
    auto *address = dialog.findChild<QLineEdit *>(QStringLiteral("analysisOllamaUrl"));
    auto *add = dialog.findChild<QPushButton *>(QStringLiteral("analysisAddOllama"));
    auto *remove = dialog.findChild<QPushButton *>(QStringLiteral("analysisRemoveOllama"));
    QVERIFY(servers && address && add && remove);
    QCOMPARE(servers->count(), 2);
    QVERIFY(!dialog.findChild<QLineEdit *>(QStringLiteral("analysisCodexCli")));

    address->setText(QStringLiteral("https://third-server:11434"));
    add->click();
    QCOMPARE(servers->count(), 3);
    QCOMPARE(servers->item(2)->text(), QStringLiteral("https://third-server:11434"));
    servers->setCurrentRow(1);
    remove->click();
    QCOMPARE(servers->count(), 2);
    const QStringList expectedUrls{QStringLiteral("http://127.0.0.1:11434"), QStringLiteral("https://third-server:11434")};
    QCOMPARE(dialog.editedSettings().analysisOllamaUrls, expectedUrls);
    QString error;
    QVERIFY2(AppSettings::save(dialog.editedSettings(), &error), qPrintable(error));
    QCOMPARE(AppSettings::loadFromPath(settings.settingsPath).analysisOllamaUrls, expectedUrls);
}

void CoreBehaviorTest::meetingAnalysisShouldSaveVersionsAndDetectChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    struct RestoreEnvironment {
        QByteArray path = qgetenv("PATH"), key = qgetenv("OPENAI_API_KEY"), fail = qgetenv("TEST_ANALYSIS_FAIL"), hang = qgetenv("TEST_ANALYSIS_HANG");
        bool hasKey = qEnvironmentVariableIsSet("OPENAI_API_KEY"), hasFail = qEnvironmentVariableIsSet("TEST_ANALYSIS_FAIL"), hasHang = qEnvironmentVariableIsSet("TEST_ANALYSIS_HANG");
        ~RestoreEnvironment() {
            qputenv("PATH", path);
            if (hasKey) qputenv("OPENAI_API_KEY", key); else qunsetenv("OPENAI_API_KEY");
            if (hasFail) qputenv("TEST_ANALYSIS_FAIL", fail); else qunsetenv("TEST_ANALYSIS_FAIL");
            if (hasHang) qputenv("TEST_ANALYSIS_HANG", hang); else qunsetenv("TEST_ANALYSIS_HANG");
        }
    } restore;
    const QByteArray fakePython =
            "#!/bin/sh\n"
            "[ -z \"$OPENAI_API_KEY\" ] && [ -z \"$CODEX_API_KEY\" ] || exit 8\n"
            "if [ \"$2\" = --list-models ]; then\n"
            "  case \" $* \" in *' --backend codex '*) printf '%s\\n' '{\"models\":[\"gpt-6-luna\",\"gpt-custom\"]}' ;; *' --backend opencode '*) printf '%s\\n' '{\"models\":[\"opencode-go/test\"]}' ;; *' --endpoint http://127.0.0.1:11434 '*) printf '%s\\n' '{\"models\":[\"local-only\"]}' ;; *) printf '%s\\n' '{\"models\":[\"qwen3:8b\",\"local-custom\"]}' ;; esac\n"
            "  exit 0\nfi\n"
            "case \" $* \" in *' --backend codex '*) case \" $* \" in *' --model gpt-custom '*) ;; *) exit 11 ;; esac ;; *' --endpoint http://127.0.0.1:11434 '*' --model local-only '*) ;; *) case \" $* \" in *' --model local-custom '*) ;; *) exit 12 ;; esac ;; esac\n"
            "case \" $* \" in *' --backend codex '*) case \" $* \" in *' --reasoning-effort xhigh '*) ;; *) exit 13 ;; esac ;; *) case \" $* \" in *' --reasoning-effort '*) exit 14 ;; esac ;; esac\n"
            "IFS= read -r first\n[ \"$first\" = '[00:00:01] Ja: Test.' ] || exit 10\ncat > /dev/null\n"
            "if [ \"$TEST_ANALYSIS_HANG\" = 1 ]; then exec sleep 30; fi\n"
            "if [ \"$TEST_ANALYSIS_FAIL\" = 1 ]; then echo 'synthetic error' >&2; exit 1; fi\n"
            "printf 'ANALYSIS_PROGRESS {\"stage\":\"Test\",\"current\":1,\"total\":1}\\n' >&2\n"
            "printf '%s\\n' '{\"markdown\":\"# Analiza spotkania\\n\\n- Wniosek (00:00:01)\\n\"}'\n";
    QVERIFY(!createExecutable(&dir, QStringLiteral("python3"), fakePython).isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + restore.path);
    qputenv("OPENAI_API_KEY", "should-not-reach-local");
    AppSettings settings;
    settings.meetingsDirectory = dir.filePath(QStringLiteral("meetings"));
    settings.analysisOllamaUrls = {QStringLiteral("http://karaczan:11434"), QStringLiteral("http://127.0.0.1:11434")};
    settings.settingsPath = dir.filePath(QStringLiteral("settings.ini"));
    MeetingEntry entry; QString error;
    QVERIFY(MeetingStore::create(settings.meetingsDirectory, &entry, &error));
    QFile source(MeetingStore::transcriptPath(entry));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("[00:00:01] Ja: Test.\n"); source.close();
    MeetingAnalysisDialog dialog(entry, settings);
    auto *start = dialog.findChild<QPushButton *>(QStringLiteral("analysisStart"));
    auto *versions = dialog.findChild<QListWidget *>(QStringLiteral("analysisVersions"));
    auto *preview = dialog.findChild<QPlainTextEdit *>(QStringLiteral("analysisPreview"));
    auto *info = dialog.findChild<QLabel *>(QStringLiteral("analysisVersionInfo"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("analysisStatus"));
    auto *backend = dialog.findChild<QComboBox *>(QStringLiteral("analysisBackend"));
    auto *model = dialog.findChild<QComboBox *>(QStringLiteral("analysisModel"));
    auto *reasoning = dialog.findChild<QComboBox *>(QStringLiteral("analysisReasoningEffort"));
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("analysisCancel"));
    QVERIFY(start && versions && preview && info && status && backend && model && reasoning && cancel);
    QCOMPARE(backend->count(), 4);
    QCOMPARE(backend->findData(QStringLiteral("openai")), -1);
    QTRY_COMPARE_WITH_TIMEOUT(model->count(), 2, 5000);
    model->setCurrentIndex(model->findText(QStringLiteral("local-custom")));
    QSignalSpy busySpy(&dialog, &MeetingAnalysisDialog::busyChanged);
    start->click();
    QTRY_COMPARE_WITH_TIMEOUT(versions->count(), 1, 5000);
    QVERIFY(versions->item(0)->text().contains(QLatin1Char('\n')));
    QVERIFY(versions->item(0)->sizeHint().height() >= versions->fontMetrics().lineSpacing() * 2);
    QVERIFY(preview->toPlainText().contains(QStringLiteral("Wniosek (00:00:01)")));
    QVERIFY(versions->currentItem()->text().contains(QStringLiteral("Duration: ")));
    QFile savedAnalysis(versions->currentItem()->data(Qt::UserRole).toString());
    QVERIFY(savedAnalysis.open(QIODevice::ReadOnly));
    QVERIFY(savedAnalysis.readAll().contains("\"duration_ms\""));
    QCOMPARE(info->text(), QStringLiteral("Based on the current transcript."));
    QCOMPARE(busySpy.size(), 2);
    QVERIFY(busySpy[0][0].toBool()); QVERIFY(!busySpy[1][0].toBool());
    start->click();
    QTRY_COMPARE_WITH_TIMEOUT(versions->count(), 2, 5000);
    qputenv("TEST_ANALYSIS_FAIL", "1");
    start->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("synthetic error")), 5000);
    QCOMPARE(versions->count(), 2);
    qunsetenv("TEST_ANALYSIS_FAIL");
    backend->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(model->findText(QStringLiteral("local-only")) >= 0, 5000);
    backend->setCurrentIndex(backend->findData(QStringLiteral("opencode")));
    QTRY_VERIFY_WITH_TIMEOUT(model->findText(QStringLiteral("opencode-go/test")) >= 0, 5000);
    backend->setCurrentIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(model->currentText(), QStringLiteral("local-custom"), 5000);
    backend->setCurrentIndex(backend->findData(QStringLiteral("codex")));
    QTRY_VERIFY_WITH_TIMEOUT(model->findText(QStringLiteral("gpt-custom")) >= 0, 5000);
    model->setCurrentIndex(model->findText(QStringLiteral("gpt-custom")));
    reasoning->setCurrentIndex(reasoning->findData(QStringLiteral("xhigh")));
    start->click();
    QTRY_COMPARE_WITH_TIMEOUT(versions->count(), 3, 5000);
    QVERIFY(versions->currentItem()->text().contains(QStringLiteral("gpt-custom")));
    QVERIFY(versions->currentItem()->text().contains(QStringLiteral("xhigh")));
    qputenv("TEST_ANALYSIS_HANG", "1");
    start->click();
    QTRY_VERIFY_WITH_TIMEOUT(cancel->isEnabled(), 2000);
    cancel->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("Analysis canceled")), 5000);
    QCOMPARE(versions->count(), 3);
    qunsetenv("TEST_ANALYSIS_HANG");
    QVERIFY(source.open(QIODevice::Append)); source.write("[00:00:02] Inna wypowiedź.\n"); source.close();
    const QString legacyPath = QDir(entry.directory).filePath(QStringLiteral("analyses/20000101-000000-000-legacy.md"));
    QFile legacy(legacyPath); QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write("<!-- STC_ANALYSIS {\"backend\":\"openai\",\"model\":\"old-model\"} -->\n# Older analysis\n"); legacy.close();
    MeetingAnalysisDialog reopened(entry, settings);
    auto *stale = reopened.findChild<QLabel *>(QStringLiteral("analysisVersionInfo"));
    auto *reopenedReasoning = reopened.findChild<QComboBox *>(QStringLiteral("analysisReasoningEffort"));
    auto *reopenedVersions = reopened.findChild<QListWidget *>(QStringLiteral("analysisVersions"));
    auto *reopenedPreview = reopened.findChild<QPlainTextEdit *>(QStringLiteral("analysisPreview"));
    QVERIFY(stale && reopenedReasoning && reopenedVersions && reopenedPreview);
    QCOMPARE(reopenedReasoning->currentData().toString(), QStringLiteral("xhigh"));
    QCOMPARE(stale->text(), QStringLiteral("This analysis is based on an older version of the transcript."));
    QCOMPARE(reopenedVersions->count(), 4);
    for (int i = 0; i < reopenedVersions->count(); ++i)
        if (reopenedVersions->item(i)->data(Qt::UserRole).toString() == legacyPath) reopenedVersions->setCurrentRow(i);
    QVERIFY(reopenedPreview->toPlainText().contains(QStringLiteral("Older analysis")));

    settings = AppSettings::loadFromPath(dir.filePath(QStringLiteral("settings.ini")));
    QCOMPARE(settings.analysisOllamaModel, QStringLiteral("local-custom"));
    QCOMPARE(QSettings(settings.settingsPath, QSettings::IniFormat).value(QStringLiteral("analysis_codex_model")).toString(), QStringLiteral("gpt-custom"));
    QCOMPARE(QSettings(settings.settingsPath, QSettings::IniFormat).value(QStringLiteral("analysis_codex_reasoning_effort")).toString(), QStringLiteral("xhigh"));
    PreferencesDialog preferences(settings);
    QVERIFY(!preferences.findChild<QLineEdit *>(QStringLiteral("analysisOpenAiModel")));
    QVERIFY(!preferences.findChild<QLineEdit *>(QStringLiteral("analysisOllamaModel")));
    QVERIFY(!preferences.findChild<QLineEdit *>(QStringLiteral("analysisOpenAiKey")));
    QVERIFY(!preferences.findChild<QLineEdit *>(QStringLiteral("analysisCodexCli")));
    settings = preferences.editedSettings();
    QFile key(dir.filePath(QStringLiteral("openai-api-key")));
    QVERIFY(key.open(QIODevice::WriteOnly)); key.write("legacy-key"); key.close();
    QVERIFY2(AppSettings::save(settings, &error), qPrintable(error));
    AppSettings loaded = AppSettings::loadFromPath(settings.settingsPath);
    QVERIFY(!QSettings(settings.settingsPath, QSettings::IniFormat).contains(QStringLiteral("analysis_codex_cli")));
    QVERIFY(key.open(QIODevice::ReadOnly)); QCOMPARE(key.readAll(), QByteArray("legacy-key"));
}

void CoreBehaviorTest::meetingSpeakerTranscriptionShouldSaveAndRename()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray oldPython = qgetenv("STC_DIARIZATION_PYTHON");
    const bool hadPython = qEnvironmentVariableIsSet("STC_DIARIZATION_PYTHON");
    struct RestoreEnvironment {
        QByteArray value; bool wasSet;
        ~RestoreEnvironment() { if (wasSet) qputenv("STC_DIARIZATION_PYTHON", value); else qunsetenv("STC_DIARIZATION_PYTHON"); }
    } restore{oldPython, hadPython};
    const QString python = createExecutable(&dir, QStringLiteral("python"),
            "#!/bin/sh\n[ \"$HF_TOKEN\" = hf_test ] && [ \"$HF_HOME\" = /test/cache ] || exit 2\n"
            "printf 'STC_PROGRESS {\"stage\":\"transcribe_' >&2\nsleep 0.1\n"
            "printf 'remote\",\"percent\":42}\\n' >&2\nsleep 1.3\n"
            "printf '%s\\n' '{\"segments\":[{\"start\":0,\"end\":1,\"speaker\":\"Ja\",\"text\":\"Hello\"},{\"start\":2,\"end\":3,\"speaker\":\"Osoba 1\",\"text\":\"Hi\"},{\"start\":4,\"end\":5,\"speaker\":\"Osoba 1\",\"text\":\"Again\"}]}'\n");
    QVERIFY(!python.isEmpty());
    qputenv("STC_DIARIZATION_PYTHON", python.toUtf8());
    AppSettings settings;
    settings.meetingsDirectory = dir.filePath(QStringLiteral("meetings"));
    settings.huggingFaceToken = QStringLiteral("hf_test");
    settings.huggingFaceHome = QStringLiteral("/test/cache");
    MeetingEntry entry; QString error;
    QVERIFY2(MeetingStore::create(settings.meetingsDirectory, &entry, &error), qPrintable(error));
    entry.status = QStringLiteral("Ready");
    QVERIFY(MeetingStore::save(entry, &error));
    QFile audio(MeetingStore::audioPath(entry));
    QVERIFY(audio.open(QIODevice::WriteOnly));
    audio.write("audio"); audio.close();
    WhisperRunner runner;
    MeetingDialog dialog(settings, &runner, [] { return true; });
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("meetingTable"));
    auto *transcribe = dialog.findChild<QPushButton *>(QStringLiteral("meetingTranscribeSpeakers"));
    auto *rename = dialog.findChild<QPushButton *>(QStringLiteral("meetingRenameSpeaker"));
    auto *progress = dialog.findChild<QProgressBar *>(QStringLiteral("meetingTranscriptionProgress"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("meetingStatus"));
    QVERIFY(table && transcribe && rename && progress && status);
    table->selectRow(0);
    QVERIFY(transcribe->isEnabled());
    QElapsedTimer timing; timing.start();
    transcribe->click();
    QTRY_COMPARE_WITH_TIMEOUT(progress->value(), 42, 4000);
    QVERIFY(status->text().contains(QStringLiteral("Transcribing remote speakers: 42%")));
    QVERIFY(status->text().contains(QStringLiteral("Elapsed:")));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.isBusy(), 5000);
    QVERIFY(timing.elapsed() >= 1000);
    QVERIFY(status->text().contains(QStringLiteral("Speaker transcription saved. Elapsed:")));
    QVERIFY(progress->isHidden());
    QFile transcript(MeetingStore::transcriptPath(entry));
    QVERIFY(transcript.open(QIODevice::ReadOnly));
    const QByteArray original = transcript.readAll(); transcript.close();
    QVERIFY(original.contains("Ja: Hello"));
    QVERIFY(original.contains("Osoba 1: Hi"));
    QVERIFY(rename->isEnabled());
    int prompt = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&prompt]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *input = qobject_cast<QInputDialog *>(widget);
            if (input && input->isVisible()) {
                if (++prompt == 2) input->setTextValue(QStringLiteral("Ania"));
                input->accept();
                break;
            }
        }
    });
    timer.start(10);
    rename->click();
    timer.stop();
    QCOMPARE(prompt, 2);
    QVERIFY(transcript.open(QIODevice::ReadOnly));
    const QByteArray renamed = transcript.readAll();
    QVERIFY(renamed.contains("Ania: Hi"));
    QVERIFY(renamed.contains("Ania: Again"));
    QVERIFY(renamed.contains("Ja: Hello"));
}

void CoreBehaviorTest::meetingTableShouldWrapStatusTextWithoutElision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MeetingEntry entry;
    QString error;
    QVERIFY(MeetingStore::create(dir.filePath(QStringLiteral("meetings")), &entry, &error));
    entry.status = QStringLiteral("Transcription interrupted");
    QVERIFY2(MeetingStore::save(entry, &error), qPrintable(error));

    AppSettings settings;
    settings.meetingsDirectory = dir.filePath(QStringLiteral("meetings"));
    WhisperRunner runner;
    MeetingDialog dialog(settings, &runner, []() { return true; });
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("meetingTable"));
    QVERIFY(table);

    QVERIFY(table->wordWrap());
    QCOMPARE(table->textElideMode(), Qt::ElideNone);
    QVERIFY(table->verticalHeader()->sectionResizeMode(0) == QHeaderView::ResizeToContents);
    table->setColumnWidth(3, 90);
    table->resizeRowsToContents();
    QVERIFY(table->rowHeight(0) >= table->fontMetrics().lineSpacing() * 2);
}

void CoreBehaviorTest::meetingRecordingAndTranscriptionShouldComplete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    struct RestorePath { QByteArray path = qgetenv("PATH"); ~RestorePath() { qputenv("PATH", path); } } restorePath;
    const QByteArray pactl =
            "#!/bin/sh\n"
            "case \"$*\" in\n"
            "  'list short sources') printf '1\\tmic\\tmodule\\n2\\tspeaker.monitor\\tmodule\\n' ;;\n"
            "  'get-default-source') echo mic ;;\n"
            "  'get-default-sink') echo speaker ;;\n"
            "esac\n";
    const QByteArray ffmpeg =
            "#!/bin/sh\n"
            "for arg in \"$@\"; do last=\"$arg\"; done\n"
            "printf audio > \"$last\"\n"
            "case \"$*\" in *'-f pulse'*) read line ;; esac\n"
            "exit 0\n";
    const QByteArray whisper = "#!/bin/sh\nprintf 'progress =  50%%\\n' >&2\nsleep 0.3\nprintf '[00:00:00.000 --> 00:00:01.000] Hello meeting.\\n'\n";
    QVERIFY(!createExecutable(&dir, QStringLiteral("pactl"), pactl).isEmpty());
    QVERIFY(!createExecutable(&dir, QStringLiteral("ffmpeg"), ffmpeg).isEmpty());
    const QString cli = createExecutable(&dir, QStringLiteral("whisper-cli"), whisper);
    QVERIFY(!cli.isEmpty());
    qputenv("PATH", dir.path().toUtf8() + ':' + restorePath.path);
    AppSettings settings;
    settings.meetingsDirectory = dir.filePath(QStringLiteral("meetings"));
    settings.whisperCli = cli;
    settings.model = dir.filePath(QStringLiteral("model.bin"));
    settings.language = QStringLiteral("en");
    WhisperRunner runner;
    MeetingDialog dialog(settings, &runner, []() { return true; });
    dialog.show();
    QSignalSpy activitySpy(&dialog, &MeetingDialog::activityChanged);
    bool sawTranscribingStatus = false;
    connect(&dialog, &MeetingDialog::busyChanged, &dialog, [&]() {
        if (dialog.activityStatus() == QStringLiteral("Transcribing meeting.")) sawTranscribingStatus = true;
    });
    auto *record = dialog.findChild<QPushButton *>(QStringLiteral("meetingRecord"));
    auto *stop = dialog.findChild<QPushButton *>(QStringLiteral("meetingStop"));
    auto *transcribe = dialog.findChild<QPushButton *>(QStringLiteral("meetingTranscribe"));
    auto *hideToTray = dialog.findChild<QPushButton *>(QStringLiteral("meetingHideToTray"));
    auto *closeWindow = dialog.findChild<QPushButton *>(QStringLiteral("meetingCloseWindow"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("meetingTable"));
    auto *recordingIndicator = dialog.findChild<QLabel *>(QStringLiteral("meetingRecordingIndicator"));
    auto *headerTitle = dialog.findChild<QLabel *>(QStringLiteral("meetingHeaderTitle"));
    auto *detailsTitle = dialog.findChild<QLabel *>(QStringLiteral("meetingDetailsTitle"));
    auto *detailsStatus = dialog.findChild<QLabel *>(QStringLiteral("meetingDetailsStatus"));
    auto *detailsLocation = dialog.findChild<QLabel *>(QStringLiteral("meetingDetailsLocation"));
    auto *detailsStarted = dialog.findChild<QLabel *>(QStringLiteral("meetingDetailsStarted"));
    auto *detailsDuration = dialog.findChild<QLabel *>(QStringLiteral("meetingDetailsDuration"));
    auto *filesTable = dialog.findChild<QTableWidget *>(QStringLiteral("meetingFilesTable"));
    auto *progress = dialog.findChild<QProgressBar *>(QStringLiteral("meetingTranscriptionProgress"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("meetingStatus"));
    QVERIFY(record && stop && transcribe && hideToTray && closeWindow && table);
    QVERIFY(recordingIndicator && headerTitle && detailsTitle && detailsStatus && detailsLocation && detailsStarted && detailsDuration && filesTable);
    QVERIFY(!headerTitle->font().bold());
    QVERIFY(table->columnWidth(0) <= 220);
    QVERIFY(dialog.width() >= 1100);
    QVERIFY(dialog.height() >= 650);
    QVERIFY(record->isEnabled());
    record->click();
    QVERIFY(dialog.isBusy());
    QVERIFY(dialog.isRecording());
    QVERIFY(dialog.canStopRecording());
    QVERIFY(dialog.activityStatus().startsWith(QStringLiteral("Recording meeting — 00:00:")));
    QTRY_COMPARE_WITH_TIMEOUT(MeetingStore::list(settings.meetingsDirectory).size(), 1, 5000);
    const MeetingEntry entry = MeetingStore::list(settings.meetingsDirectory).first();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(MeetingStore::audioPath(entry)).size() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!activitySpy.isEmpty(), 2000);
    table->selectRow(0);
    QCOMPARE(detailsTitle->text(), entry.name);
    QCOMPARE(detailsStatus->text(), QStringLiteral("Recording"));
    QTRY_COMPARE(detailsLocation->text(), entry.directory);
    QVERIFY(detailsStarted->text() != QStringLiteral("—"));
    QVERIFY(detailsDuration->text().contains(QLatin1Char(':')));
    QVERIFY(recordingIndicator->isVisible());
    QVERIFY(recordingIndicator->text().contains(QStringLiteral("REC")));
    QCOMPARE(filesTable->columnCount(), 3);
    QTRY_VERIFY([&]() {
        for (int row = 0; row < filesTable->rowCount(); ++row) {
            const auto *name = filesTable->item(row, 0);
            const auto *size = filesTable->item(row, 1);
            const auto *modified = filesTable->item(row, 2);
            if (name && name->text() == QStringLiteral("audio.flac"))
                return size && !size->text().isEmpty() && modified && !modified->text().isEmpty();
        }
        return false;
    }());
    hideToTray->click();
    QVERIFY(dialog.isHidden());
    QVERIFY(dialog.isRecording());
    dialog.show();
    QVERIFY(recordingIndicator->isVisible());
    closeWindow->click();
    QVERIFY(dialog.isHidden());
    QVERIFY(dialog.isRecording());
    dialog.show();
    QVERIFY(recordingIndicator->isVisible());
    dialog.stopRecording();
    QVERIFY(dialog.isRecording());
    QVERIFY(!dialog.canStopRecording());
    QCOMPARE(dialog.activityStatus(), QStringLiteral("Finishing meeting recording…"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.isBusy(), 5000);
    QVERIFY(!dialog.isRecording());
    QVERIFY(!recordingIndicator->isVisible());
    table->selectRow(0);
    QTRY_VERIFY(transcribe->isEnabled());
    transcribe->click();
    QVERIFY(progress && status);
    QTRY_COMPARE_WITH_TIMEOUT(progress->value(), 50, 5000);
    QVERIFY(status->text().contains(QStringLiteral("Transcribing meeting: 50%")));
    QVERIFY(status->text().contains(QStringLiteral("Elapsed:")));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(MeetingStore::transcriptPath(entry)), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.isBusy(), 5000);
    QVERIFY(status->text().contains(QStringLiteral("Transcription saved. Elapsed:")));
    QVERIFY(progress->isHidden());
    QVERIFY(sawTranscribingStatus);
    QFile transcript(MeetingStore::transcriptPath(entry));
    QVERIFY(transcript.open(QIODevice::ReadOnly));
    QCOMPARE(transcript.readAll(), QByteArray("[00:00:00.000 --> 00:00:01.000] Hello meeting.\n"));
    QCOMPARE(MeetingStore::list(settings.meetingsDirectory).first().status, QStringLiteral("Ready"));

    const QByteArray brokenFfmpeg = "#!/bin/sh\necho 'synthetic failure' >&2\nexit 1\n";
    QVERIFY(!createExecutable(&dir, QStringLiteral("ffmpeg"), brokenFfmpeg).isEmpty());
    QTimer closer;
    connect(&closer, &QTimer::timeout, []() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
        }
    });
    closer.start(20);
    table->selectRow(0);
    transcribe->click();
    QTRY_COMPARE_WITH_TIMEOUT(MeetingStore::list(settings.meetingsDirectory).first().status,
            QStringLiteral("Transcription failed"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.isBusy(), 5000);
    QVERIFY(QFileInfo::exists(MeetingStore::audioPath(entry)));
    QFile previousTranscript(MeetingStore::transcriptPath(entry));
    QVERIFY(previousTranscript.open(QIODevice::ReadOnly));
    QCOMPARE(previousTranscript.readAll(), QByteArray("[00:00:00.000 --> 00:00:01.000] Hello meeting.\n"));

    const QByteArray interruptedFfmpeg =
            "#!/bin/sh\nfor arg in \"$@\"; do last=\"$arg\"; done\nprintf partial > \"$last\"\n"
            "echo 'audio source disconnected' >&2\nexit 1\n";
    QVERIFY(!createExecutable(&dir, QStringLiteral("ffmpeg"), interruptedFfmpeg).isEmpty());
    record->click();
    QTRY_COMPARE_WITH_TIMEOUT(MeetingStore::list(settings.meetingsDirectory).size(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.isBusy(), 5000);
    bool interruptedFound = false;
    for (const auto &saved : MeetingStore::list(settings.meetingsDirectory)) {
        if (saved.status == QStringLiteral("Recording interrupted")) {
            interruptedFound = true;
            QFile partial(MeetingStore::audioPath(saved));
            QVERIFY(partial.open(QIODevice::ReadOnly));
            QCOMPARE(partial.readAll(), QByteArray("partial"));
        }
    }
    QVERIFY(interruptedFound);
}

void CoreBehaviorTest::meetingAudioFilterShouldPreserveSeparateChannels()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("FFmpeg is not installed");
    QProcess ffmpeg;
    const QString filter = QStringLiteral("[0:a]aresample=48000:async=1:first_pts=0,aformat=sample_fmts=s16:channel_layouts=mono[m];[1:a]aresample=48000:async=1:first_pts=0,aformat=sample_fmts=s16:channel_layouts=mono[o];[m][o]amerge=inputs=2[out]");
    ffmpeg.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=48000:duration=0.1"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=880:sample_rate=48000:duration=0.1"),
            QStringLiteral("-filter_complex"), filter, QStringLiteral("-map"), QStringLiteral("[out]"),
            QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-")});
    QVERIFY(ffmpeg.waitForFinished(5000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    const QByteArray samples = ffmpeg.readAllStandardOutput();
    QVERIFY(samples.size() >= 400);
    const uchar *data = reinterpret_cast<const uchar *>(samples.constData());
    const int16_t left = qFromLittleEndian<int16_t>(data + 24 * 4);
    const int16_t right = qFromLittleEndian<int16_t>(data + 24 * 4 + 2);
    QVERIFY(left != right);
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

void CoreBehaviorTest::whisperRunnerShouldStreamMeetingProgress()
{
    QTemporaryDir dir;
    const QString cli = createExecutable(&dir, QStringLiteral("whisper-cli"),
            "#!/bin/sh\n"
            "case \" $* \" in *' -pp '*) ;; *) exit 3 ;; esac\n"
            "printf 'whisper_print_progress_callback: progress = ' >&2\n"
            "sleep 0.1\n"
            "printf ' 42%%\\nprogress = 20%%\\nprogress = 101%%\\n' >&2\n"
            "printf '[00:00:00.000 --> 00:00:01.000] Hello.\\n'\n");
    QVERIFY(!cli.isEmpty());
    AppSettings settings;
    settings.whisperCli = cli; settings.model = dir.filePath(QStringLiteral("model"));
    settings.language = QStringLiteral("pl"); settings.threads = 2;
    WhisperRunner runner;
    QSignalSpy progressSpy(&runner, &WhisperRunner::progressChanged);
    QSignalSpy readySpy(&runner, &WhisperRunner::transcriptionReady);
    runner.transcribe(dir.filePath(QStringLiteral("audio.wav")), settings, true);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(progressSpy.size(), 1);
    QCOMPARE(progressSpy.first().first().toInt(), 42);
    QCOMPARE(readySpy.first().first().toString(), QStringLiteral("[00:00:00.000 --> 00:00:01.000] Hello."));
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
