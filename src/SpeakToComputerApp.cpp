#include "SpeakToComputerApp.h"

#include "WavWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

SpeakToComputerApp::SpeakToComputerApp(const AppSettings &settings, QObject *parent)
    : QObject(parent)
    , settings_(settings)
{
    connect(&hotkey_, &X11Hotkey::activated, this, &SpeakToComputerApp::toggleDictation);
    connect(&recorder_, &AudioRecorder::levelChanged, &overlay_, &OverlayWidget::setAudioLevel);
    connect(&recorder_, &AudioRecorder::failed, this, &SpeakToComputerApp::handleRecordingFailed);
    connect(&whisper_, &WhisperRunner::transcriptionReady, this, &SpeakToComputerApp::handleTranscriptionReady);
    connect(&whisper_, &WhisperRunner::failed, this, &SpeakToComputerApp::handleTranscriptionFailed);
    connect(&elapsedTimer_, &QTimer::timeout, this, [this]() {
        overlay_.setElapsedMs(recordingClock_.elapsed());
    });
    elapsedTimer_.setInterval(200);
}

bool SpeakToComputerApp::start()
{
    QString errorMessage;
    if (!hotkey_.registerHotkey(settings_.hotkey, &errorMessage)) {
        qWarning().noquote() << errorMessage;
        overlay_.showError(errorMessage);
        return false;
    }
    qInfo().noquote() << "speak-to-computer listening for" << settings_.hotkey;
    qInfo().noquote() << "settings:" << settings_.settingsPath;
    return true;
}

void SpeakToComputerApp::toggleDictation()
{
    if (hotkeyDebounce_.isValid() && hotkeyDebounce_.elapsed() < 500) {
        return;
    }
    hotkeyDebounce_.restart();

    if (state_ == State::Idle) {
        startRecording();
    } else if (state_ == State::Recording) {
        stopRecording();
    }
}

void SpeakToComputerApp::startRecording()
{
    QString errorMessage;
    if (!validateRuntime(&errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    targetWindow_ = hotkey_.activeWindow();
    if (!recorder_.start(settings_.audioBackend, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    state_ = State::Recording;
    recordingClock_.restart();
    elapsedTimer_.start();
    overlay_.showRecording();
    qInfo().noquote() << "recording started using audio backend" << recorder_.activeBackendName();
}

void SpeakToComputerApp::stopRecording()
{
    elapsedTimer_.stop();

    QString errorMessage;
    const QByteArray pcm = recorder_.stop(&errorMessage);
    if (!errorMessage.isEmpty()) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }
    if (pcm.isEmpty()) {
        showErrorAndReturnIdle(QStringLiteral("No audio was captured from the microphone."));
        return;
    }

    currentWavPath_ = nextWavPath();
    if (!WavWriter::writePcm16Mono(currentWavPath_, pcm, 16000, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    state_ = State::Transcribing;
    overlay_.showTranscribing();
    qInfo() << "recording stopped, transcribing";
    whisper_.transcribe(currentWavPath_, settings_);
}

void SpeakToComputerApp::handleTranscriptionReady(const QString &text)
{
    removeCurrentWav();

    if (text.isEmpty()) {
        showErrorAndReturnIdle(QStringLiteral("Whisper did not return any text."));
        return;
    }

    QString errorMessage;
    if (!paster_.paste(targetWindow_, text, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    state_ = State::Idle;
    overlay_.showDone(QStringLiteral("Text pasted into the active window"));
    qInfo() << "transcription pasted";
    QTimer::singleShot(900, &overlay_, &OverlayWidget::hide);
}

void SpeakToComputerApp::handleTranscriptionFailed(const QString &message)
{
    removeCurrentWav();
    showErrorAndReturnIdle(message);
}

void SpeakToComputerApp::handleRecordingFailed(const QString &message)
{
    if (state_ == State::Recording) {
        elapsedTimer_.stop();
        showErrorAndReturnIdle(message);
    }
}

bool SpeakToComputerApp::validateRuntime(QString *errorMessage) const
{
    if (!AudioRecorder::hasSupportedBackend(settings_.audioBackend, errorMessage)) {
        return false;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool was not found in PATH.");
        }
        return false;
    }
    if (!QFileInfo(settings_.whisperCli).isExecutable()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("whisper-cli is missing or not executable: %1").arg(settings_.whisperCli);
        }
        return false;
    }
    if (!QFileInfo::exists(settings_.model)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Whisper model is missing: %1").arg(settings_.model);
        }
        return false;
    }
    return true;
}

QString SpeakToComputerApp::nextWavPath() const
{
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString fileName = QStringLiteral("speak-to-computer-%1-%2.wav")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch());
    return QDir(tempRoot).filePath(fileName);
}

void SpeakToComputerApp::showErrorAndReturnIdle(const QString &message)
{
    state_ = State::Idle;
    overlay_.showError(message);
}

void SpeakToComputerApp::removeCurrentWav()
{
    if (!currentWavPath_.isEmpty()) {
        QFile::remove(currentWavPath_);
        currentWavPath_.clear();
    }
}
