#pragma once

#include "AppSettings.h"
#include "AudioRecorder.h"
#include "ClipboardPaster.h"
#include "OverlayWidget.h"
#include "WhisperRunner.h"
#include "X11Hotkey.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

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
    void showErrorAndReturnIdle(const QString &message);
    void removeCurrentWav();

    AppSettings settings_;
    X11Hotkey hotkey_;
    OverlayWidget overlay_;
    AudioRecorder recorder_;
    WhisperRunner whisper_;
    ClipboardPaster paster_;
    QTimer elapsedTimer_;
    QElapsedTimer recordingClock_;
    QElapsedTimer hotkeyDebounce_;
    State state_ = State::Idle;
    quint64 targetWindow_ = 0;
    QString currentWavPath_;
};
