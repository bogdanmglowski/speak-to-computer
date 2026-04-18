#include "SpeakToComputerApp.h"

#include "WavWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
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
    connect(&overlay_, &OverlayWidget::modelSelected, this, &SpeakToComputerApp::handleModelSelected);
    connect(&elapsedTimer_, &QTimer::timeout, this, [this]() {
        overlay_.setElapsedMs(recordingClock_.elapsed());
    });
    elapsedTimer_.setInterval(200);

    overlay_.setModelLabel(AppSettings::modelLabel(settings_.model));
    overlay_.setAvailableModelPaths(AppSettings::existingModelPaths(settings_.model));
    overlay_.setModelControlEnabled(true);
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
    overlay_.setAvailableModelPaths(AppSettings::existingModelPaths(settings_.model));

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
    overlay_.setModelControlEnabled(true);
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
    overlay_.setModelControlEnabled(false);
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
    overlay_.setModelControlEnabled(true);
    overlay_.showDone(QStringLiteral("Text pasted into the active window"));
    qInfo() << "transcription pasted";
    QTimer::singleShot(900, &overlay_, &OverlayWidget::hide);
}

void SpeakToComputerApp::handleTranscriptionFailed(const QString &message)
{
    const QString fallbackPath = fallbackModelPathForInitializationFailure();
    const bool shouldOfferFallback =
            message.contains(QStringLiteral("failed to initialize whisper context"), Qt::CaseInsensitive)
            && !fallbackPath.isEmpty();
    if (shouldOfferFallback) {
        const QString failureMessage = message;
        QTimer::singleShot(0, this, [this, failureMessage]() {
            if (!maybeOfferModelFallback(failureMessage)) {
                removeCurrentWav();
                showErrorAndReturnIdle(failureMessage);
            }
        });
        return;
    }

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

void SpeakToComputerApp::handleModelSelected(const QString &modelPath)
{
    QString errorMessage;
    if (!applyModelSelection(modelPath, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            return;
        }
        qWarning().noquote() << errorMessage;
        return;
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
    const QFileInfo modelFileInfo(settings_.model);
    if (!modelFileInfo.isFile() || modelFileInfo.size() <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Whisper model is invalid or empty: %1").arg(settings_.model);
        }
        return false;
    }
    return true;
}

QString SpeakToComputerApp::fallbackModelPathForInitializationFailure() const
{
    const QFileInfo currentModelInfo(settings_.model);
    if (!currentModelInfo.exists() || !currentModelInfo.isFile() || currentModelInfo.size() <= 0) {
        return QString();
    }

    const QStringList modelPaths = AppSettings::existingModelPaths(settings_.model);
    qint64 bestCandidateSize = -1;
    QString bestCandidatePath;
    for (const QString &modelPath : modelPaths) {
        if (modelPath == settings_.model) {
            continue;
        }
        const QFileInfo modelInfo(modelPath);
        if (!modelInfo.exists() || !modelInfo.isFile() || modelInfo.size() <= 0) {
            continue;
        }
        if (modelInfo.size() >= currentModelInfo.size()) {
            continue;
        }
        if (modelInfo.size() > bestCandidateSize) {
            bestCandidateSize = modelInfo.size();
            bestCandidatePath = modelPath;
        }
    }
    return bestCandidatePath;
}

bool SpeakToComputerApp::applyModelSelection(const QString &selectedModelPath, QString *errorMessage)
{
    if (selectedModelPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Model selection is invalid.");
        }
        return false;
    }
    if (selectedModelPath == settings_.model) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return false;
    }
    if (!AppSettings::saveModel(settings_.settingsPath, selectedModelPath, errorMessage)) {
        return false;
    }

    settings_.model = selectedModelPath;
    overlay_.setModelLabel(AppSettings::modelLabel(settings_.model));
    overlay_.setAvailableModelPaths(AppSettings::existingModelPaths(settings_.model));
    qInfo().noquote() << "selected whisper model" << settings_.model;
    return true;
}

bool SpeakToComputerApp::maybeOfferModelFallback(const QString &message)
{
    if (!message.contains(QStringLiteral("failed to initialize whisper context"), Qt::CaseInsensitive)) {
        return false;
    }

    const QString fallbackPath = fallbackModelPathForInitializationFailure();
    if (fallbackPath.isEmpty()) {
        return false;
    }

    const QString fallbackLabel = AppSettings::modelLabel(fallbackPath);
    const QString prompt = QStringLiteral(
            "Whisper could not initialize model \"%1\".\n\n"
            "Do you want to switch to \"%2\" for the next recording?")
                                   .arg(AppSettings::modelLabel(settings_.model), fallbackLabel);
    const QMessageBox::StandardButton choice = QMessageBox::question(
            nullptr,
            QStringLiteral("Retry with smaller model"),
            prompt,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
    if (choice != QMessageBox::Yes) {
        return false;
    }

    QString errorMessage;
    if (!applyModelSelection(fallbackPath, &errorMessage)) {
        if (!errorMessage.isEmpty()) {
            qWarning().noquote() << errorMessage;
        }
        return false;
    }

    removeCurrentWav();
    state_ = State::Idle;
    overlay_.setModelControlEnabled(true);
    overlay_.showDone(QStringLiteral("Switched to %1. Press hotkey to retry.").arg(fallbackLabel));
    QTimer::singleShot(1400, &overlay_, &OverlayWidget::hide);
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
    overlay_.setModelControlEnabled(true);
    overlay_.showError(message);
}

void SpeakToComputerApp::removeCurrentWav()
{
    if (!currentWavPath_.isEmpty()) {
        QFile::remove(currentWavPath_);
        currentWavPath_.clear();
    }
}
