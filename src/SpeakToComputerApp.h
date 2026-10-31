#pragma once

#include "AppSettings.h"
#include "AudioRecorder.h"
#include "BuzzAsrRunner.h"
#include "ClipboardPaster.h"
#include "FileHandoff.h"
#include "OverlayWidget.h"
#include "ParakeetRunner.h"
#include "RealtimeSidecar.h"
#include "VadEndpointDetector.h"
#include "WakeWordListener.h"
#include "WhisperRunner.h"
#include "X11Hotkey.h"

#include <QElapsedTimer>
#include <QMenu>
#include <QObject>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

class QAction;

class SpeakToComputerApp : public QObject {
    Q_OBJECT

public:
    explicit SpeakToComputerApp(const AppSettings &settings, QObject *parent = nullptr);

    bool start();

private slots:
    void handleTranscriptionReady(const QString &text);
    void handleTranscriptionFailed(const QString &message);
    void handleRecordingFailed(const QString &message);
    void handleModelSelected(const QString &modelPath);
    void handleEngineSelected(const QString &engineId);
    void handleLanguageSelected(const QString &languageCode);
    void handleSendKeywordToggled(bool enabled);
    void handlePasteTargetSelected(const QString &pasteTargetId);
    void handleMiniOverlayToggled(bool enabled);
    void showPreferences();
    void handleWakeWordDetected();
    void handleWakeWordFailure(const QString &message);
    void handleWakeWordToggled(bool enabled);
    void handleVadAutostopToggled(bool enabled);
    void handleVadPresetSelected(bool enabled, int endSilenceMs);
    void handleRealtimeSessionFailed(const QString &message);
    void handleRealtimeServerFailed(const QString &message);
    void quitApplication();

private:
    enum class State {
        Idle,
        Recording,
        Transcribing,
    };

    enum class OutputMode {
        Original,
        English,
    };

    enum class OutputTarget {
        Clipboard,
        HandoffFile,
    };

    void handleHotkey(OutputMode outputMode, const X11Hotkey &sourceHotkey);
    void startRecording(OutputMode outputMode, quint64 targetWindow);
    void stopRecording(OutputMode outputMode);
    void cancelRecording();
    void updateWakeWordListening();
    void stopWakeWordListening();
    void showWakeWordListeningStatus();
    void disableWakeWordWithError(const QString &message);
    void refreshVadRuntimeStatus();
    bool validateRuntime(QString *errorMessage) const;
    QString fallbackModelPathForInitializationFailure() const;
    bool applyModelSelection(const QString &selectedModelPath, QString *errorMessage);
    bool applyEngineSelection(const QString &engineId, QString *errorMessage);
    bool applyLanguageSelection(const QString &languageCode, QString *errorMessage);
    bool applySettings(const AppSettings &newSettings, QString *errorMessage);
    bool registerPushToTalkButton(int button, QString *errorMessage);
    bool maybeOfferModelFallback(const QString &message);
    QString outputLabel(OutputMode outputMode) const;
    QString gpuMemoryLabel() const;
    QString originalLanguageLabel() const;
    QString hotkeyFor(OutputMode outputMode) const;
    QStringList recordingHints() const;
    QString nextWavPath() const;
    QString trayStatusText() const;
    void setupTrayIcon();
    void setOverlayCloseHotkeyEnabled(bool enabled);
    void updateTrayStatus();
    void updateOverlayVadControl();
    void configureVadForCurrentRecording();
    void showAlertAndReturnIdle(const QString &message);
    void showErrorAndReturnIdle(const QString &message);
    void removeCurrentWav();
    void playActivationSound();
    void playEndSound();
    void refreshModelUi();
    void refreshEngineUi();
    void refreshLanguageUi();
    void refreshSendKeywordUi();
    void refreshPasteTargetUi();
    quint64 effectivePasteWindow() const;
    void setOverlayRuntimeControlsEnabled(bool enabled);
    bool ensureRealtimeServerLoaded(QString *errorMessage);
    void unloadRealtimeServer();
    void startRealtimeSession(OutputMode outputMode, quint64 targetWindow);
    void stopRealtimeSession();

    AppSettings settings_;
    X11Hotkey dictateHotkey_;
    X11Hotkey mouseDictateHotkey_;
    X11Hotkey mouseFinishHotkey_;
    X11Hotkey pushToTalkHotkey_;
    bool sendEnterAfterTranscription_ = false;
    bool pushToTalkActive_ = false;
    bool pushToTalkTranscription_ = false;
    X11Hotkey translateHotkey_;
    X11Hotkey overlayCloseHotkey_;
    OverlayWidget overlay_;
    AudioRecorder recorder_;
    WhisperRunner whisper_;
    BuzzAsrRunner buzzAsr_;
    ParakeetRunner parakeet_;
    ClipboardPaster paster_;
    FileHandoff handoff_;
    RealtimeSidecar realtime_;
    QMenu trayMenu_;
    QSystemTrayIcon trayIcon_;
    QTimer elapsedTimer_;
    QElapsedTimer recordingClock_;
    QElapsedTimer hotkeyDebounce_;
    QAction *trayStatusAction_ = nullptr;
    QAction *trayPreferencesAction_ = nullptr;
    QAction *trayCopyLastTextAction_ = nullptr;
    QAction *trayMiniOverlayAction_ = nullptr;
    QAction *trayWakeWordAction_ = nullptr;
    QAction *trayVadAutostopAction_ = nullptr;
    QAction *trayQuitAction_ = nullptr;
    State state_ = State::Idle;
    OutputMode currentOutputMode_ = OutputMode::Original;
    quint64 targetWindow_ = 0;
    QString currentWavPath_;
    QString lastTranscription_;
    QString trayStatusOverride_;
    WakeWordListener wakeWordListener_;
    VadEndpointDetector vadEndpointDetector_;
    bool wakeWordAvailable_ = true;
    bool vadEnabledForCurrentRecording_ = false;
    bool vadRuntimeAvailable_ = true;
    QString vadRuntimeError_;
    bool recordingStopRequested_ = false;
    bool overlayCloseHotkeyEnabled_ = false;
    bool engineSwitchInProgress_ = false;
};
