#pragma once

#include "AppSettings.h"
#include "AudioRecorder.h"
#include "ClipboardPaster.h"
#include "OverlayWidget.h"
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
    void handleWakeWordDetected();
    void handleWakeWordFailure(const QString &message);
    void handleWakeWordToggled(bool enabled);
    void handleVadAutostopToggled(bool enabled);
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

    void handleHotkey(OutputMode outputMode, const X11Hotkey &sourceHotkey);
    void startRecording(OutputMode outputMode, quint64 targetWindow);
    void stopRecording(OutputMode outputMode);
    void updateWakeWordListening();
    void stopWakeWordListening();
    void showWakeWordListeningStatus();
    void disableWakeWordWithError(const QString &message);
    void refreshVadRuntimeStatus();
    bool validateRuntime(QString *errorMessage) const;
    QString fallbackModelPathForInitializationFailure() const;
    bool applyModelSelection(const QString &selectedModelPath, QString *errorMessage);
    bool maybeOfferModelFallback(const QString &message);
    QString outputLabel(OutputMode outputMode) const;
    QString originalLanguageLabel() const;
    QString hotkeyFor(OutputMode outputMode) const;
    QStringList recordingHints() const;
    QString nextWavPath() const;
    QString trayStatusText() const;
    void setupTrayIcon();
    void updateTrayStatus();
    void showAlertAndReturnIdle(const QString &message);
    void showErrorAndReturnIdle(const QString &message);
    void removeCurrentWav();
    void playActivationSound();
    void playEndSound();

    AppSettings settings_;
    X11Hotkey dictateHotkey_;
    X11Hotkey translateHotkey_;
    OverlayWidget overlay_;
    AudioRecorder recorder_;
    WhisperRunner whisper_;
    ClipboardPaster paster_;
    QMenu trayMenu_;
    QSystemTrayIcon trayIcon_;
    QTimer elapsedTimer_;
    QElapsedTimer recordingClock_;
    QElapsedTimer hotkeyDebounce_;
    QAction *trayStatusAction_ = nullptr;
    QAction *trayWakeWordAction_ = nullptr;
    QAction *trayVadAutostopAction_ = nullptr;
    QAction *trayQuitAction_ = nullptr;
    State state_ = State::Idle;
    OutputMode currentOutputMode_ = OutputMode::Original;
    quint64 targetWindow_ = 0;
    QString currentWavPath_;
    QString trayStatusOverride_;
    WakeWordListener wakeWordListener_;
    VadEndpointDetector vadEndpointDetector_;
    bool wakeWordAvailable_ = true;
    bool vadEnabledForCurrentRecording_ = false;
    bool vadRuntimeAvailable_ = true;
    QString vadRuntimeError_;
    bool recordingStopRequested_ = false;
};
