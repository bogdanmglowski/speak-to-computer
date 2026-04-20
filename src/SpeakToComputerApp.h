#pragma once

#include "AppSettings.h"
#include "AudioRecorder.h"
#include "ClipboardPaster.h"
#include "OverlayWidget.h"
#include "WhisperRunner.h"
#include "X11Hotkey.h"

#include <QElapsedTimer>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>

class QAction;

class SpeakToComputerApp : public QObject {
    Q_OBJECT

public:
    explicit SpeakToComputerApp(const AppSettings &settings, QObject *parent = nullptr);

    bool start();

private slots:
    void toggleDictation();
    void startRecording();
    void stopRecording();
    void handleTranscriptionReady(const QString &text);
    void handleTranscriptionFailed(const QString &message);
    void handleRecordingFailed(const QString &message);
    void handleModelSelected(const QString &modelPath);
    void quitApplication();

private:
    enum class State {
        Idle,
        Recording,
        Transcribing,
    };

    bool validateRuntime(QString *errorMessage) const;
    QString fallbackModelPathForInitializationFailure() const;
    bool applyModelSelection(const QString &selectedModelPath, QString *errorMessage);
    bool maybeOfferModelFallback(const QString &message);
    QString nextWavPath() const;
    QString trayStatusText() const;
    void setupTrayIcon();
    void updateTrayStatus();
    void showErrorAndReturnIdle(const QString &message);
    void removeCurrentWav();
    void playActivationSound();
    void playEndSound();

    AppSettings settings_;
    X11Hotkey hotkey_;
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
    QAction *trayQuitAction_ = nullptr;
    State state_ = State::Idle;
    quint64 targetWindow_ = 0;
    QString currentWavPath_;
    QString trayStatusOverride_;
};
