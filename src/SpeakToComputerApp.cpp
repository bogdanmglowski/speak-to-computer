#include "SpeakToComputerApp.h"

#include "WebRtcVad.h"
#include "WavWriter.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QSignalBlocker>
#include <QLocale>
#include <QStandardPaths>
#include <QSystemTrayIcon>

namespace {

QIcon createApplicationIcon()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128}) {
        const qreal unit = static_cast<qreal>(size) / 64.0;
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(229, 70, 78));
        painter.drawEllipse(QPointF(32.0 * unit, 32.0 * unit), 20.0 * unit, 20.0 * unit);

        painter.setBrush(QColor(245, 247, 250));
        painter.drawRoundedRect(QRectF(24.0 * unit, 13.0 * unit, 16.0 * unit, 27.0 * unit),
                8.0 * unit,
                8.0 * unit);

        painter.setPen(QPen(QColor(245, 247, 250), 4.0 * unit, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(32.0 * unit, 40.0 * unit), QPointF(32.0 * unit, 50.0 * unit));
        painter.drawLine(QPointF(24.0 * unit, 50.0 * unit), QPointF(40.0 * unit, 50.0 * unit));

        painter.setPen(QPen(QColor(22, 24, 28), 3.0 * unit, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(29.0 * unit, 21.0 * unit), QPointF(35.0 * unit, 21.0 * unit));
        painter.drawLine(QPointF(29.0 * unit, 28.0 * unit), QPointF(35.0 * unit, 28.0 * unit));

        icon.addPixmap(pixmap);
    }
    return icon;
}

QString languageDisplayName(const QString &language)
{
    const QString languageTag = language.trimmed();
    if (languageTag.isEmpty()) {
        return QStringLiteral("unknown");
    }
    if (languageTag.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("auto-detected");
    }

    const QLocale locale(languageTag);
    if (locale.language() == QLocale::C) {
        return languageTag;
    }
    return QLocale::languageToString(locale.language());
}

} // namespace

SpeakToComputerApp::SpeakToComputerApp(const AppSettings &settings, QObject *parent)
    : QObject(parent)
    , settings_(settings)
{
    connect(&dictateHotkey_, &X11Hotkey::activated, this, [this]() {
        handleHotkey(OutputMode::Original, dictateHotkey_);
    });
    connect(&translateHotkey_, &X11Hotkey::activated, this, [this]() {
        handleHotkey(OutputMode::English, translateHotkey_);
    });
    connect(&recorder_, &AudioRecorder::levelChanged, &overlay_, &OverlayWidget::setAudioLevel);
    connect(&recorder_, &AudioRecorder::audioChunkCaptured, this, [this](const QByteArray &chunk) {
        if (state_ != State::Recording || !vadEnabledForCurrentRecording_ || recordingStopRequested_) {
            return;
        }

        QString errorMessage;
        if (!vadEndpointDetector_.consumePcmChunk(chunk, &errorMessage)) {
            qWarning().noquote() << "VAD auto-stop disabled for current recording:" << errorMessage;
            vadEnabledForCurrentRecording_ = false;
            return;
        }
        if (!vadEndpointDetector_.shouldAutoStop()) {
            return;
        }

        recordingStopRequested_ = true;
        QTimer::singleShot(0, this, [this]() {
            stopRecording(currentOutputMode_);
        });
    });
    connect(&recorder_, &AudioRecorder::failed, this, &SpeakToComputerApp::handleRecordingFailed);
    connect(&whisper_, &WhisperRunner::transcriptionReady, this, &SpeakToComputerApp::handleTranscriptionReady);
    connect(&whisper_, &WhisperRunner::failed, this, &SpeakToComputerApp::handleTranscriptionFailed);
    connect(&wakeWordListener_, &WakeWordListener::detected, this, &SpeakToComputerApp::handleWakeWordDetected);
    connect(&wakeWordListener_, &WakeWordListener::failed, this, &SpeakToComputerApp::handleWakeWordFailure);
    connect(&overlay_, &OverlayWidget::modelSelected, this, &SpeakToComputerApp::handleModelSelected);
    connect(&elapsedTimer_, &QTimer::timeout, this, [this]() {
        overlay_.setElapsedMs(recordingClock_.elapsed());
    });
    elapsedTimer_.setInterval(200);

    overlay_.setModelLabel(AppSettings::modelLabel(settings_.model));
    overlay_.setAvailableModelPaths(AppSettings::existingModelPaths(settings_.model));
    overlay_.setModelControlEnabled(true);

    setupTrayIcon();
}

bool SpeakToComputerApp::start()
{
    QString errorMessage;
    if (!dictateHotkey_.registerHotkey(settings_.hotkeyDictate, &errorMessage)) {
        const QString fullMessage =
                QStringLiteral("Could not register dictation hotkey %1: %2")
                        .arg(settings_.hotkeyDictate, errorMessage);
        qWarning().noquote() << fullMessage;
        overlay_.showError(fullMessage);
        trayStatusOverride_ = QStringLiteral("Error: %1").arg(fullMessage);
        updateTrayStatus();
        return false;
    }
    if (!translateHotkey_.registerHotkey(settings_.hotkeyTranslateEn, &errorMessage)) {
        dictateHotkey_.unregisterHotkey();
        const QString fullMessage =
                QStringLiteral("Could not register English hotkey %1: %2")
                        .arg(settings_.hotkeyTranslateEn, errorMessage);
        qWarning().noquote() << fullMessage;
        overlay_.showError(fullMessage);
        trayStatusOverride_ = QStringLiteral("Error: %1").arg(fullMessage);
        updateTrayStatus();
        return false;
    }
    qInfo().noquote() << "speak-to-computer listening for dictation hotkey" << settings_.hotkeyDictate
                      << "and English hotkey" << settings_.hotkeyTranslateEn;
    qInfo().noquote() << "settings:" << settings_.settingsPath;
    refreshVadRuntimeStatus();
    trayStatusOverride_.clear();
    wakeWordAvailable_ = true;
    updateWakeWordListening();
    updateTrayStatus();
    return true;
}

void SpeakToComputerApp::handleHotkey(OutputMode outputMode, const X11Hotkey &sourceHotkey)
{
    if (hotkeyDebounce_.isValid() && hotkeyDebounce_.elapsed() < 500) {
        return;
    }
    hotkeyDebounce_.restart();

    if (state_ == State::Idle) {
        startRecording(outputMode, sourceHotkey.activeWindow());
    } else if (state_ == State::Recording) {
        stopRecording(outputMode);
    }
}

void SpeakToComputerApp::startRecording(OutputMode outputMode, quint64 targetWindow)
{
    stopWakeWordListening();
    trayStatusOverride_.clear();
    overlay_.setAvailableModelPaths(AppSettings::existingModelPaths(settings_.model));
    recordingStopRequested_ = false;
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();

    QString errorMessage;
    if (!validateRuntime(&errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    currentOutputMode_ = outputMode;
    targetWindow_ = targetWindow;
    playActivationSound();
    if (!recorder_.start(settings_.audioBackend, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    if (settings_.vadAutostopEnabled) {
        refreshVadRuntimeStatus();
        if (!vadRuntimeAvailable_) {
            qWarning().noquote() << "VAD auto-stop unavailable:" << vadRuntimeError_;
        } else {
            const VadEndpointConfig vadConfig{
                    settings_.vadAggressiveness,
                    settings_.vadEndSilenceMs,
                    settings_.vadMinSpeechMs,
            };
            QString vadErrorMessage;
            if (vadEndpointDetector_.reset(vadConfig, &vadErrorMessage)) {
                vadEnabledForCurrentRecording_ = true;
            } else {
                qWarning().noquote() << "VAD auto-stop disabled:" << vadErrorMessage;
            }
        }
    }

    state_ = State::Recording;
    recordingClock_.restart();
    elapsedTimer_.start();
    overlay_.setModelControlEnabled(true);
    overlay_.showRecording(outputLabel(currentOutputMode_), recordingHints());
    updateTrayStatus();
    qInfo().noquote() << "recording started using audio backend" << recorder_.activeBackendName()
                      << "output" << outputLabel(currentOutputMode_)
                      << "vadAutoStop" << (vadEnabledForCurrentRecording_ ? "enabled" : "disabled");
}

void SpeakToComputerApp::stopRecording(OutputMode outputMode)
{
    if (state_ != State::Recording) {
        return;
    }

    recordingStopRequested_ = true;
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();
    currentOutputMode_ = outputMode;
    trayStatusOverride_.clear();
    elapsedTimer_.stop();

    QString errorMessage;
    const QByteArray pcm = recorder_.stop(&errorMessage);
    playEndSound();
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
    overlay_.showTranscribing(outputLabel(currentOutputMode_));
    updateTrayStatus();
    qInfo().noquote() << "recording stopped, transcribing output" << outputLabel(currentOutputMode_);

    AppSettings transcriptionSettings = settings_;
    transcriptionSettings.translateToEn = currentOutputMode_ == OutputMode::English;
    whisper_.transcribe(currentWavPath_, transcriptionSettings);
}

void SpeakToComputerApp::handleTranscriptionReady(const QString &text)
{
    removeCurrentWav();

    if (text.isEmpty()) {
        showAlertAndReturnIdle(QStringLiteral("Whisper did not return any text."));
        return;
    }

    QString errorMessage;
    if (!paster_.paste(targetWindow_, text, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    state_ = State::Idle;
    trayStatusOverride_.clear();
    overlay_.setModelControlEnabled(true);
    overlay_.showDone(QStringLiteral("%1 text pasted into the active window").arg(outputLabel(currentOutputMode_)));
    updateWakeWordListening();
    updateTrayStatus();
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

void SpeakToComputerApp::handleWakeWordDetected()
{
    if (state_ != State::Idle || !settings_.wakeWordEnabled || !wakeWordAvailable_) {
        return;
    }

    const quint64 activeWindow = dictateHotkey_.activeWindow();
    startRecording(OutputMode::Original, activeWindow);
}

void SpeakToComputerApp::handleWakeWordFailure(const QString &message)
{
    disableWakeWordWithError(message);
}

void SpeakToComputerApp::handleWakeWordToggled(bool enabled)
{
    QString errorMessage;
    if (!AppSettings::saveWakeWordEnabled(settings_.settingsPath, enabled, &errorMessage)) {
        qWarning().noquote() << errorMessage;
    }

    settings_.wakeWordEnabled = enabled;
    wakeWordAvailable_ = true;
    if (!enabled) {
        stopWakeWordListening();
        trayStatusOverride_.clear();
        overlay_.showDone(QStringLiteral("Wake-word listening disabled"));
        updateTrayStatus();
        QTimer::singleShot(900, &overlay_, &OverlayWidget::hide);
        return;
    }

    updateWakeWordListening();
    if (wakeWordListener_.isRunning()) {
        showWakeWordListeningStatus();
    }
}

void SpeakToComputerApp::handleVadAutostopToggled(bool enabled)
{
    QString errorMessage;
    if (!AppSettings::saveVadAutostopEnabled(settings_.settingsPath, enabled, &errorMessage)) {
        qWarning().noquote() << errorMessage;
    }
    settings_.vadAutostopEnabled = enabled;
    refreshVadRuntimeStatus();
    updateTrayStatus();
}

void SpeakToComputerApp::updateWakeWordListening()
{
    if (state_ != State::Idle || !settings_.wakeWordEnabled || !wakeWordAvailable_) {
        stopWakeWordListening();
        updateTrayStatus();
        return;
    }
    if (wakeWordListener_.isRunning()) {
        updateTrayStatus();
        return;
    }

    QString errorMessage;
    if (!wakeWordListener_.start(settings_, &errorMessage)) {
        disableWakeWordWithError(errorMessage);
        return;
    }
    showWakeWordListeningStatus();
    updateTrayStatus();
}

void SpeakToComputerApp::stopWakeWordListening()
{
    if (wakeWordListener_.isRunning()) {
        wakeWordListener_.stop();
    }
}

void SpeakToComputerApp::showWakeWordListeningStatus()
{
    if (state_ != State::Idle || !wakeWordListener_.isRunning()) {
        return;
    }

    trayStatusOverride_.clear();
    overlay_.setModelControlEnabled(true);
    overlay_.showListening(settings_.wakeWordPhrase);
    QTimer::singleShot(1100, &overlay_, &OverlayWidget::hide);
}

void SpeakToComputerApp::disableWakeWordWithError(const QString &message)
{
    stopWakeWordListening();
    wakeWordAvailable_ = false;
    settings_.wakeWordEnabled = false;
    QString saveErrorMessage;
    if (!AppSettings::saveWakeWordEnabled(settings_.settingsPath, false, &saveErrorMessage)) {
        qWarning().noquote() << saveErrorMessage;
    }
    if (trayWakeWordAction_ != nullptr) {
        const QSignalBlocker blocker(trayWakeWordAction_);
        trayWakeWordAction_->setChecked(false);
    }
    trayStatusOverride_ = QStringLiteral("Wake word disabled: %1").arg(message);
    overlay_.showError(QStringLiteral("Wake word disabled: %1").arg(message), QStringLiteral("Wake word"));
    updateTrayStatus();
}

void SpeakToComputerApp::refreshVadRuntimeStatus()
{
    if (!settings_.vadAutostopEnabled) {
        vadRuntimeAvailable_ = true;
        vadRuntimeError_.clear();
        return;
    }

    QString runtimeError;
    vadRuntimeAvailable_ = WebRtcVad::isRuntimeAvailable(&runtimeError);
    vadRuntimeError_ = runtimeError;
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
    trayStatusOverride_.clear();
    overlay_.setModelControlEnabled(true);
    overlay_.showDone(QStringLiteral("Switched to %1. Press %2 to retry.")
                              .arg(fallbackLabel, hotkeyFor(currentOutputMode_)));
    updateWakeWordListening();
    updateTrayStatus();
    QTimer::singleShot(1400, &overlay_, &OverlayWidget::hide);
    return true;
}

QString SpeakToComputerApp::outputLabel(OutputMode outputMode) const
{
    if (outputMode == OutputMode::English) {
        return QStringLiteral("English");
    }
    return QStringLiteral("Original (%1)").arg(originalLanguageLabel());
}

QString SpeakToComputerApp::originalLanguageLabel() const
{
    return languageDisplayName(settings_.language);
}

QString SpeakToComputerApp::hotkeyFor(OutputMode outputMode) const
{
    if (outputMode == OutputMode::English) {
        return settings_.hotkeyTranslateEn;
    }
    return settings_.hotkeyDictate;
}

QStringList SpeakToComputerApp::recordingHints() const
{
    return {
            QStringLiteral("%1: finish as %2").arg(hotkeyFor(OutputMode::Original), outputLabel(OutputMode::Original)),
            QStringLiteral("%1: finish as %2").arg(hotkeyFor(OutputMode::English), outputLabel(OutputMode::English)),
    };
}

QString SpeakToComputerApp::nextWavPath() const
{
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString fileName = QStringLiteral("speak-to-computer-%1-%2.wav")
            .arg(QCoreApplication::applicationPid())
            .arg(QDateTime::currentMSecsSinceEpoch());
    return QDir(tempRoot).filePath(fileName);
}

QString SpeakToComputerApp::trayStatusText() const
{
    if (!trayStatusOverride_.isEmpty()) {
        return trayStatusOverride_;
    }

    if (state_ == State::Recording) {
        return QStringLiteral("Recording %1. Press %2 to finish.")
                .arg(outputLabel(currentOutputMode_), hotkeyFor(currentOutputMode_));
    }
    if (state_ == State::Transcribing) {
        return QStringLiteral("Transcribing %1.").arg(outputLabel(currentOutputMode_));
    }
    if (settings_.wakeWordEnabled) {
        if (wakeWordListener_.isRunning()) {
            if (settings_.vadAutostopEnabled && !vadRuntimeAvailable_) {
                return QStringLiteral("Wake word ON. VAD auto-stop unavailable: missing libfvad. %1 dictates, %2 translates.")
                        .arg(settings_.hotkeyDictate, settings_.hotkeyTranslateEn);
            }
            return QStringLiteral("Listening for wake word \"%1\". %2 dictates, %3 translates to English.")
                    .arg(settings_.wakeWordPhrase, settings_.hotkeyDictate, settings_.hotkeyTranslateEn);
        }
        if (settings_.vadAutostopEnabled && !vadRuntimeAvailable_) {
            return QStringLiteral("Wake word enabled, VAD auto-stop unavailable: missing libfvad.");
        }
        return QStringLiteral("Wake word is enabled but unavailable. %1 dictates, %2 translates to English.")
                .arg(settings_.hotkeyDictate, settings_.hotkeyTranslateEn);
    }
    if (settings_.vadAutostopEnabled && !vadRuntimeAvailable_) {
        return QStringLiteral("Ready. VAD auto-stop unavailable: missing libfvad. %1 dictates, %2 translates.")
                .arg(settings_.hotkeyDictate, settings_.hotkeyTranslateEn);
    }
    return QStringLiteral("Ready. %1 dictates, %2 translates to English.")
            .arg(settings_.hotkeyDictate, settings_.hotkeyTranslateEn);
}

void SpeakToComputerApp::setupTrayIcon()
{
    const QIcon icon = createApplicationIcon();
    QApplication::setWindowIcon(icon);
    overlay_.setWindowIcon(icon);

    trayStatusAction_ = trayMenu_.addAction(trayStatusText());
    trayStatusAction_->setEnabled(false);
    trayWakeWordAction_ = trayMenu_.addAction(QStringLiteral("Wake Word Listening"));
    trayWakeWordAction_->setCheckable(true);
    trayWakeWordAction_->setChecked(settings_.wakeWordEnabled);
    connect(trayWakeWordAction_, &QAction::toggled, this, &SpeakToComputerApp::handleWakeWordToggled);
    trayVadAutostopAction_ = trayMenu_.addAction(QStringLiteral("Voice Activity Auto-Stop"));
    trayVadAutostopAction_->setCheckable(true);
    trayVadAutostopAction_->setChecked(settings_.vadAutostopEnabled);
    connect(trayVadAutostopAction_, &QAction::toggled, this, &SpeakToComputerApp::handleVadAutostopToggled);
    trayMenu_.addSeparator();
    trayQuitAction_ = trayMenu_.addAction(QStringLiteral("Quit"));
    connect(trayQuitAction_, &QAction::triggered, this, &SpeakToComputerApp::quitApplication);

    trayIcon_.setIcon(icon);
    trayIcon_.setContextMenu(&trayMenu_);
    trayIcon_.show();

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "No system tray is currently available.";
    }
    updateTrayStatus();
}

void SpeakToComputerApp::updateTrayStatus()
{
    if (trayWakeWordAction_ != nullptr && trayWakeWordAction_->isChecked() != settings_.wakeWordEnabled) {
        const QSignalBlocker blocker(trayWakeWordAction_);
        trayWakeWordAction_->setChecked(settings_.wakeWordEnabled);
    }
    if (trayVadAutostopAction_ != nullptr && trayVadAutostopAction_->isChecked() != settings_.vadAutostopEnabled) {
        const QSignalBlocker blocker(trayVadAutostopAction_);
        trayVadAutostopAction_->setChecked(settings_.vadAutostopEnabled);
    }

    const QString status = trayStatusText();
    if (trayStatusAction_ != nullptr) {
        trayStatusAction_->setText(status);
    }
    trayIcon_.setToolTip(QStringLiteral("Speak to Computer\n%1").arg(status));
}

void SpeakToComputerApp::showAlertAndReturnIdle(const QString &message)
{
    state_ = State::Idle;
    recordingStopRequested_ = false;
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();
    trayStatusOverride_ = QStringLiteral("Alert: %1").arg(message);
    overlay_.setModelControlEnabled(true);
    overlay_.showError(message, QStringLiteral("Dictation alert"));
    updateWakeWordListening();
    updateTrayStatus();
}

void SpeakToComputerApp::showErrorAndReturnIdle(const QString &message)
{
    state_ = State::Idle;
    recordingStopRequested_ = false;
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();
    trayStatusOverride_ = QStringLiteral("Error: %1").arg(message);
    overlay_.setModelControlEnabled(true);
    overlay_.showError(message);
    updateWakeWordListening();
    updateTrayStatus();
}

void SpeakToComputerApp::quitApplication()
{
    stopWakeWordListening();
    trayIcon_.hide();
    QCoreApplication::quit();
}

void SpeakToComputerApp::removeCurrentWav()
{
    if (!currentWavPath_.isEmpty()) {
        QFile::remove(currentWavPath_);
        currentWavPath_.clear();
    }
}

void SpeakToComputerApp::playActivationSound()
{
    if (settings_.activationSound.isEmpty()) {
        return;
    }
    QString resolvedPath = settings_.activationSound;
    if (!QFileInfo(resolvedPath).isAbsolute()) {
        resolvedPath = QCoreApplication::applicationDirPath() + QStringLiteral("/") + resolvedPath;
    }
    if (!QFileInfo::exists(resolvedPath)) {
        qWarning().noquote() << "activation sound not found:" << resolvedPath;
        return;
    }
    QProcess::startDetached(QStringLiteral("ffplay"), {QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), QStringLiteral("-volume"), QStringLiteral("50"), resolvedPath});
}

void SpeakToComputerApp::playEndSound()
{
    if (settings_.endSound.isEmpty()) {
        return;
    }
    QString resolvedPath = settings_.endSound;
    if (!QFileInfo(resolvedPath).isAbsolute()) {
        resolvedPath = QCoreApplication::applicationDirPath() + QStringLiteral("/") + resolvedPath;
    }
    if (!QFileInfo::exists(resolvedPath)) {
        qWarning().noquote() << "end sound not found:" << resolvedPath;
        return;
    }
    QProcess::startDetached(QStringLiteral("ffplay"), {QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), QStringLiteral("-volume"), QStringLiteral("50"), resolvedPath});
}
