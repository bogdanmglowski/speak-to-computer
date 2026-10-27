#include "SpeakToComputerApp.h"

#include "PreferencesDialog.h"
#include "WebRtcVad.h"
#include "WavWriter.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QSignalBlocker>
#include <QLocale>
#include <QStandardPaths>
#include <QSystemTrayIcon>

namespace {

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
    connect(&mouseDictateHotkey_, &X11Hotkey::activated, this, [this]() {
        handleHotkey(OutputMode::Original, mouseDictateHotkey_);
    });
    connect(&mouseFinishHotkey_, &X11Hotkey::activated, this, [this]() {
        if (state_ == State::Recording) {
            sendEnterAfterTranscription_ = true;
            stopRecording(currentOutputMode_);
        }
    });
    connect(&translateHotkey_, &X11Hotkey::activated, this, [this]() {
        handleHotkey(OutputMode::English, translateHotkey_);
    });
    connect(&overlayCloseHotkey_, &X11Hotkey::activated, this, &SpeakToComputerApp::cancelRecording);
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
    connect(&buzzAsr_, &BuzzAsrRunner::transcriptionReady, this, &SpeakToComputerApp::handleTranscriptionReady);
    connect(&buzzAsr_, &BuzzAsrRunner::failed, this, &SpeakToComputerApp::handleTranscriptionFailed);
    connect(&realtime_, &RealtimeSidecar::sessionFailed, this, &SpeakToComputerApp::handleRealtimeSessionFailed);
    connect(&realtime_, &RealtimeSidecar::serverFailed, this, &SpeakToComputerApp::handleRealtimeServerFailed);
    connect(&realtime_, &RealtimeSidecar::transcriptionUpdated, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty()) {
            lastTranscription_ = text;
            trayCopyLastTextAction_->setEnabled(true);
        }
    });
    connect(&wakeWordListener_, &WakeWordListener::detected, this, &SpeakToComputerApp::handleWakeWordDetected);
    connect(&wakeWordListener_, &WakeWordListener::failed, this, &SpeakToComputerApp::handleWakeWordFailure);
    connect(&overlay_, &OverlayWidget::modelSelected, this, &SpeakToComputerApp::handleModelSelected);
    connect(&overlay_, &OverlayWidget::engineSelected, this, &SpeakToComputerApp::handleEngineSelected);
    connect(&overlay_, &OverlayWidget::languageSelected, this, &SpeakToComputerApp::handleLanguageSelected);
    connect(&overlay_, &OverlayWidget::sendKeywordToggled, this, &SpeakToComputerApp::handleSendKeywordToggled);
    connect(&overlay_, &OverlayWidget::pasteTargetSelected, this, &SpeakToComputerApp::handlePasteTargetSelected);
    connect(&overlay_, &OverlayWidget::vadPresetSelected, this, &SpeakToComputerApp::handleVadPresetSelected);
    connect(&overlay_, &OverlayWidget::closeRequested, this, &SpeakToComputerApp::cancelRecording);
    connect(&overlay_, &OverlayWidget::visibilityChanged, this, &SpeakToComputerApp::setOverlayCloseHotkeyEnabled);
    connect(&elapsedTimer_, &QTimer::timeout, this, [this]() {
        overlay_.setElapsedMs(recordingClock_.elapsed());
        if (state_ == State::Recording) {
            overlay_.setGpuMemoryLabel(gpuMemoryLabel());
        }
    });
    elapsedTimer_.setInterval(200);

    refreshModelUi();
    refreshEngineUi();
    refreshLanguageUi();
    refreshSendKeywordUi();
    refreshPasteTargetUi();
    overlay_.setMiniModeEnabled(settings_.miniOverlayEnabled);
    setOverlayRuntimeControlsEnabled(true);
    refreshVadRuntimeStatus();
    updateOverlayVadControl();

    setupTrayIcon();
}

void SpeakToComputerApp::refreshModelUi()
{
    if (settings_.isBuzzAsrEngine()) {
        // BuzzASR ships one fixed Polish model, so the whisper model picker does not apply.
        overlay_.setModelLabel(QStringLiteral("polish"));
        overlay_.setAvailableModelPaths({});
        overlay_.setModelControlEnabled(false);
        return;
    }
    overlay_.setModelControlEnabled(true);
    if (settings_.isRealtimeEngine()) {
        overlay_.setModelLabel(AppSettings::realtimeModelLabel(settings_.realtimeModel));
        // Overlay expects file paths; use synthetic paths "realtime:<name>" so selection round-trips.
        QStringList paths;
        for (const QString &name : AppSettings::realtimeModelChoices()) {
            paths << (QStringLiteral("realtime:") + name);
        }
        overlay_.setAvailableModelPaths(paths);
    } else {
        overlay_.setModelLabel(AppSettings::modelLabel(settings_.model));
        overlay_.setAvailableModelPaths(AppSettings::existingModelPaths(settings_.model));
    }
}

void SpeakToComputerApp::refreshEngineUi()
{
    overlay_.setEngineLabel(AppSettings::engineLabel(settings_.engine));
}

void SpeakToComputerApp::refreshLanguageUi()
{
    overlay_.setLanguageLabel(AppSettings::languageLabel(settings_.language));
}

void SpeakToComputerApp::refreshSendKeywordUi()
{
    overlay_.setSendKeyword(settings_.sendKeyword, settings_.sendKeywordEnabled);
}

void SpeakToComputerApp::refreshPasteTargetUi()
{
    overlay_.setPasteTargetLabel(AppSettings::pasteTargetLabel(settings_.pasteTarget));
}

quint64 SpeakToComputerApp::effectivePasteWindow() const
{
    // Focus mode: leave target as 0 so xdotool pastes into whatever currently has focus.
    if (settings_.isFocusPasteTarget()) {
        return 0;
    }
    return targetWindow_;
}

void SpeakToComputerApp::setOverlayRuntimeControlsEnabled(bool enabled)
{
    overlay_.setModelControlEnabled(enabled);
    overlay_.setEngineControlEnabled(enabled);
    overlay_.setLanguageControlEnabled(enabled);
    overlay_.setSendKeywordControlEnabled(enabled);
    overlay_.setPasteTargetControlEnabled(enabled);
}

bool SpeakToComputerApp::start()
{
    BuzzAsrRunner::killOrphanedDaemons();
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
    if (!mouseDictateHotkey_.registerMouseButton(10, &errorMessage)) {
        qWarning().noquote() << errorMessage;
    } else {
        qInfo() << "listening for dictation mouse button 10";
    }
    qInfo().noquote() << "settings:" << settings_.settingsPath
                      << "engine:" << settings_.engine;
    refreshVadRuntimeStatus();
    trayStatusOverride_.clear();
    wakeWordAvailable_ = true;
    updateWakeWordListening();
    updateTrayStatus();

    if (settings_.isRealtimeEngine()) {
        trayStatusOverride_ = QStringLiteral("Loading Realtime model…");
        updateTrayStatus();
        QString loadError;
        if (!ensureRealtimeServerLoaded(&loadError)) {
            qWarning().noquote() << loadError;
            trayStatusOverride_ = QStringLiteral("Realtime load failed (see logs)");
            updateTrayStatus();
            overlay_.showError(loadError, QStringLiteral("Realtime engine"));
        } else {
            trayStatusOverride_.clear();
            updateTrayStatus();
        }
    }

    return true;
}

void SpeakToComputerApp::handleHotkey(OutputMode outputMode, const X11Hotkey &sourceHotkey)
{
    if (engineSwitchInProgress_) {
        return;
    }
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
    refreshModelUi();
    recordingStopRequested_ = false;
    vadEnabledForCurrentRecording_ = false;
    sendEnterAfterTranscription_ = false;
    vadEndpointDetector_.clear();

    QString errorMessage;
    if (!validateRuntime(&errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    if (settings_.isRealtimeEngine()) {
        startRealtimeSession(outputMode, targetWindow);
        return;
    }
    if (settings_.isBuzzAsrEngine() && outputMode == OutputMode::English) {
        showErrorAndReturnIdle(
                QStringLiteral("BuzzASR is Polish only and does not support Translate-to-English. "
                               "Use the whisper.cpp engine or the dictation hotkey."));
        return;
    }

    currentOutputMode_ = outputMode;
    targetWindow_ = targetWindow;
    playActivationSound();
    if (!recorder_.start(settings_.audioBackend, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    refreshVadRuntimeStatus();
    updateOverlayVadControl();
    configureVadForCurrentRecording();

    state_ = State::Recording;
    if (!mouseFinishHotkey_.registerMouseButton(9, &errorMessage)) {
        qWarning().noquote() << errorMessage;
    }
    recordingClock_.restart();
    elapsedTimer_.start();
    setOverlayRuntimeControlsEnabled(true);
    overlay_.showRecording(outputLabel(currentOutputMode_), recordingHints());
    updateOverlayVadControl();
    updateTrayStatus();
    qInfo().noquote() << "recording started using audio backend" << recorder_.activeBackendName()
                      << "output" << outputLabel(currentOutputMode_)
                      << "vadAutoStop" << (vadEnabledForCurrentRecording_ ? "enabled" : "disabled");
}

void SpeakToComputerApp::startRealtimeSession(OutputMode outputMode, quint64 targetWindow)
{
    if (outputMode == OutputMode::English) {
        // SimulStreaming translate task can be wired later; keep UX clear.
        showErrorAndReturnIdle(
                QStringLiteral("Realtime engine does not support Translate-to-English yet. "
                               "Use whisper.cpp engine or the dictation hotkey."));
        return;
    }

    QString errorMessage;
    if (!ensureRealtimeServerLoaded(&errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    currentOutputMode_ = outputMode;
    targetWindow_ = targetWindow;
    playActivationSound();
    if (!realtime_.startSession(targetWindow, settings_, &errorMessage)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    state_ = State::Recording;
    recordingClock_.restart();
    elapsedTimer_.start();
    setOverlayRuntimeControlsEnabled(true);
    overlay_.showRecording(QStringLiteral("Realtime · live"),
            {QStringLiteral("%1 again to stop").arg(settings_.hotkeyDictate)});
    updateOverlayVadControl();
    updateTrayStatus();
    qInfo().noquote() << "realtime session started model" << settings_.realtimeModel;
}

void SpeakToComputerApp::stopRealtimeSession()
{
    if (state_ != State::Recording || !settings_.isRealtimeEngine()) {
        return;
    }
    recordingStopRequested_ = true;
    elapsedTimer_.stop();
    realtime_.stopSession();
    playEndSound();
    state_ = State::Idle;
    trayStatusOverride_.clear();
    setOverlayRuntimeControlsEnabled(true);
    overlay_.showDone(QStringLiteral("Realtime session stopped"));
    updateWakeWordListening();
    updateTrayStatus();
    QTimer::singleShot(900, &overlay_, &OverlayWidget::hide);
    qInfo() << "realtime session stopped by hotkey";
}

void SpeakToComputerApp::stopRecording(OutputMode outputMode)
{
    if (state_ != State::Recording) {
        return;
    }

    if (settings_.isRealtimeEngine()) {
        Q_UNUSED(outputMode);
        stopRealtimeSession();
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
    setOverlayRuntimeControlsEnabled(false);
    overlay_.showTranscribing(outputLabel(currentOutputMode_));
    updateTrayStatus();
    qInfo().noquote() << "recording stopped, transcribing output" << outputLabel(currentOutputMode_);

    if (settings_.isBuzzAsrEngine()) {
        buzzAsr_.transcribe(currentWavPath_, settings_);
        return;
    }
    AppSettings transcriptionSettings = settings_;
    transcriptionSettings.translateToEn = currentOutputMode_ == OutputMode::English;
    whisper_.transcribe(currentWavPath_, transcriptionSettings);
}

void SpeakToComputerApp::cancelRecording()
{
    if (state_ != State::Recording) {
        overlay_.hide();
        return;
    }

    recordingStopRequested_ = true;
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();
    elapsedTimer_.stop();

    if (settings_.isRealtimeEngine()) {
        realtime_.stopSession();
        playEndSound();
        state_ = State::Idle;
        trayStatusOverride_.clear();
        updateWakeWordListening();
        updateTrayStatus();
        overlay_.hide();
        qInfo().noquote() << "realtime session cancelled";
        return;
    }

    QString errorMessage;
    recorder_.stop(&errorMessage);
    playEndSound();
    removeCurrentWav();

    state_ = State::Idle;
    trayStatusOverride_.clear();
    updateWakeWordListening();
    updateTrayStatus();
    overlay_.hide();
    qInfo().noquote() << "recording cancelled";
}

bool SpeakToComputerApp::ensureRealtimeServerLoaded(QString *errorMessage)
{
    return realtime_.loadServer(settings_, errorMessage);
}

void SpeakToComputerApp::unloadRealtimeServer()
{
    realtime_.unloadServer();
}

void SpeakToComputerApp::handleRealtimeSessionFailed(const QString &message)
{
    if (state_ == State::Recording && settings_.isRealtimeEngine()) {
        elapsedTimer_.stop();
        showErrorAndReturnIdle(message);
    }
}

void SpeakToComputerApp::handleRealtimeServerFailed(const QString &message)
{
    qWarning().noquote() << message;
    if (state_ == State::Recording && settings_.isRealtimeEngine()) {
        elapsedTimer_.stop();
        showErrorAndReturnIdle(message);
    } else {
        trayStatusOverride_ = QStringLiteral("Realtime server stopped");
        updateTrayStatus();
    }
}

void SpeakToComputerApp::handleTranscriptionReady(const QString &text)
{
    removeCurrentWav();

    if (text.isEmpty()) {
        showAlertAndReturnIdle(QStringLiteral("Whisper did not return any text."));
        return;
    }

    bool useHandoff = settings_.outputTarget == QStringLiteral("handoff_file");
    QString handoffText = text;

    if (settings_.outputTarget == QStringLiteral("auto")) {
        const QString trimmed = text.trimmed();
        const QStringList triggerWords = settings_.handoffTriggerWords.split(QLatin1Char(','), Qt::SkipEmptyParts);
        useHandoff = false;
        for (const QString &word : triggerWords) {
            const QString trimmedWord = word.trimmed();
            if (trimmedWord.isEmpty()) {
                continue;
            }
            if (!trimmed.startsWith(trimmedWord, Qt::CaseInsensitive)) {
                continue;
            }
            QString afterTrigger = trimmed.mid(trimmedWord.length());
            while (!afterTrigger.isEmpty() && QStringLiteral(" ,.:;!?-").contains(afterTrigger.at(0))) {
                afterTrigger.remove(0, 1);
            }
            if (!afterTrigger.isEmpty()) {
                useHandoff = true;
                handoffText = afterTrigger;
            }
            break;
        }
    }

    QString errorMessage;
    if (useHandoff) {
        if (!handoff_.write(settings_.handoffDirectory, handoffText, &errorMessage)) {
            showErrorAndReturnIdle(errorMessage);
            return;
        }
        state_ = State::Idle;
        trayStatusOverride_.clear();
        setOverlayRuntimeControlsEnabled(true);
        overlay_.showDone(QStringLiteral("%1 text written to %2").arg(outputLabel(currentOutputMode_), settings_.handoffDirectory));
        updateWakeWordListening();
        updateTrayStatus();
        qInfo() << "transcription handoff";
        QTimer::singleShot(900, &overlay_, &OverlayWidget::hide);
        return;
    }

    qInfo().noquote() << "text:" << text;
    lastTranscription_ = text;
    trayCopyLastTextAction_->setEnabled(true);
    if (!paster_.paste(effectivePasteWindow(), text, &errorMessage, sendEnterAfterTranscription_)) {
        showErrorAndReturnIdle(errorMessage);
        return;
    }

    state_ = State::Idle;
    trayStatusOverride_.clear();
    setOverlayRuntimeControlsEnabled(true);
    const QString pasteWhere = settings_.isFocusPasteTarget()
            ? QStringLiteral("current focus")
            : QStringLiteral("pinned window");
    overlay_.showDone(QStringLiteral("%1 text pasted into %2")
                              .arg(outputLabel(currentOutputMode_), pasteWhere));
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
    if (engineSwitchInProgress_) {
        return;
    }
    QString errorMessage;
    if (!applyModelSelection(modelPath, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            return;
        }
        qWarning().noquote() << errorMessage;
        return;
    }
}

void SpeakToComputerApp::handleEngineSelected(const QString &engineId)
{
    if (engineSwitchInProgress_) {
        return;
    }
    if (state_ == State::Transcribing) {
        return;
    }

    QString errorMessage;
    if (!applyEngineSelection(engineId, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            return;
        }
        qWarning().noquote() << errorMessage;
        showErrorAndReturnIdle(errorMessage);
    }
}

void SpeakToComputerApp::handleLanguageSelected(const QString &languageCode)
{
    if (engineSwitchInProgress_) {
        return;
    }
    if (state_ == State::Transcribing) {
        return;
    }

    QString errorMessage;
    if (!applyLanguageSelection(languageCode, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            return;
        }
        qWarning().noquote() << errorMessage;
        showErrorAndReturnIdle(errorMessage);
    }
}

void SpeakToComputerApp::handleSendKeywordToggled(bool enabled)
{
    if (engineSwitchInProgress_ || state_ == State::Transcribing) {
        return;
    }
    if (settings_.sendKeywordEnabled == enabled) {
        return;
    }

    QString errorMessage;
    if (!AppSettings::saveSendKeywordEnabled(settings_.settingsPath, enabled, &errorMessage)) {
        qWarning().noquote() << errorMessage;
        return;
    }
    settings_.sendKeywordEnabled = enabled;
    realtime_.setSendKeywordEnabled(enabled);
    refreshSendKeywordUi();
    qInfo().noquote() << "send keyword" << settings_.sendKeyword << (enabled ? "enabled" : "disabled");
}

void SpeakToComputerApp::handlePasteTargetSelected(const QString &pasteTargetId)
{
    if (engineSwitchInProgress_ || state_ == State::Transcribing) {
        return;
    }

    const QString next = AppSettings::normalizePasteTarget(pasteTargetId);
    if (next == AppSettings::normalizePasteTarget(settings_.pasteTarget)) {
        return;
    }

    QString errorMessage;
    if (!AppSettings::savePasteTarget(settings_.settingsPath, next, &errorMessage)) {
        qWarning().noquote() << errorMessage;
        return;
    }
    settings_.pasteTarget = next;

    // Switching to Pin mid-session re-pins whatever currently has focus.
    if (settings_.isPinPasteTarget()
            && (state_ == State::Recording || state_ == State::Idle)) {
        const quint64 active = dictateHotkey_.activeWindow();
        if (active != 0) {
            targetWindow_ = active;
        }
    }

    realtime_.setPasteTarget(settings_.pasteTarget);
    realtime_.setTargetWindow(targetWindow_);
    refreshPasteTargetUi();
    qInfo().noquote() << "paste target" << settings_.pasteTarget;
}

bool SpeakToComputerApp::applyEngineSelection(const QString &engineId, QString *errorMessage)
{
    const QString nextEngine = AppSettings::normalizeEngine(engineId);
    const QString previousEngine = AppSettings::normalizeEngine(settings_.engine);
    if (nextEngine == previousEngine) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return false;
    }

    const bool wasRecording = state_ == State::Recording;
    const OutputMode resumeMode = currentOutputMode_;
    const quint64 resumeWindow = targetWindow_;

    engineSwitchInProgress_ = true;
    setOverlayRuntimeControlsEnabled(false);

    if (wasRecording) {
        // Discard in-progress audio / live session; restart fresh after switch.
        recordingStopRequested_ = true;
        vadEnabledForCurrentRecording_ = false;
        vadEndpointDetector_.clear();
        elapsedTimer_.stop();

        if (settings_.isRealtimeEngine()) {
            realtime_.stopSession();
        } else {
            QString stopError;
            recorder_.stop(&stopError);
            removeCurrentWav();
        }

        playEndSound();
        state_ = State::Idle;
        trayStatusOverride_.clear();
    }

    if (!AppSettings::saveEngine(settings_.settingsPath, nextEngine, errorMessage)) {
        engineSwitchInProgress_ = false;
        setOverlayRuntimeControlsEnabled(true);
        return false;
    }
    settings_.engine = nextEngine;
    refreshEngineUi();
    refreshModelUi();

    // Always drop Realtime server when leaving Live or when entering Batch.
    if (previousEngine == QString::fromUtf8(AppSettings::kEngineRealtime)
            || nextEngine == QString::fromUtf8(AppSettings::kEngineWhisperCpp)) {
        unloadRealtimeServer();
    }

    if (settings_.isRealtimeEngine()) {
        trayStatusOverride_ = QStringLiteral("Loading Realtime model…");
        updateTrayStatus();
        QString loadError;
        if (!ensureRealtimeServerLoaded(&loadError)) {
            if (errorMessage != nullptr) {
                *errorMessage = loadError;
            }
            trayStatusOverride_ = QStringLiteral("Realtime load failed");
            updateTrayStatus();
            engineSwitchInProgress_ = false;
            setOverlayRuntimeControlsEnabled(true);
            return false;
        }
        trayStatusOverride_.clear();
        updateTrayStatus();
    }

    engineSwitchInProgress_ = false;
    setOverlayRuntimeControlsEnabled(true);

    qInfo().noquote() << "engine switched from" << previousEngine << "to" << nextEngine
                      << (wasRecording ? "(restart session)" : "(idle)");

    if (wasRecording) {
        // Realtime has no translate mode — fall back to Original dictation.
        OutputMode mode = resumeMode;
        if (settings_.isRealtimeEngine() && mode == OutputMode::English) {
            mode = OutputMode::Original;
            qInfo() << "engine switch: Translate-to-English not supported on Realtime; using Original";
        }
        startRecording(mode, resumeWindow);
        return true;
    }

    updateWakeWordListening();
    updateTrayStatus();
    return true;
}

bool SpeakToComputerApp::applyLanguageSelection(const QString &languageCode, QString *errorMessage)
{
    const QString nextLanguage = AppSettings::normalizeLanguage(languageCode);
    const QString previousLanguage = AppSettings::normalizeLanguage(settings_.language);
    if (nextLanguage == previousLanguage) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return false;
    }

    const bool wasRecording = state_ == State::Recording;
    const OutputMode resumeMode = currentOutputMode_;
    const quint64 resumeWindow = targetWindow_;

    engineSwitchInProgress_ = true;
    setOverlayRuntimeControlsEnabled(false);

    if (wasRecording) {
        recordingStopRequested_ = true;
        vadEnabledForCurrentRecording_ = false;
        vadEndpointDetector_.clear();
        elapsedTimer_.stop();

        if (settings_.isRealtimeEngine()) {
            realtime_.stopSession();
        } else {
            QString stopError;
            recorder_.stop(&stopError);
            removeCurrentWav();
        }

        playEndSound();
        state_ = State::Idle;
        trayStatusOverride_.clear();
    }

    if (!AppSettings::saveLanguage(settings_.settingsPath, nextLanguage, errorMessage)) {
        engineSwitchInProgress_ = false;
        setOverlayRuntimeControlsEnabled(true);
        return false;
    }
    settings_.language = nextLanguage;
    refreshLanguageUi();

    // Live server bakes --language at startup; reload when realtime is active.
    if (settings_.isRealtimeEngine()) {
        unloadRealtimeServer();
        trayStatusOverride_ = QStringLiteral("Reloading language…");
        updateTrayStatus();
        QString loadError;
        if (!ensureRealtimeServerLoaded(&loadError)) {
            if (errorMessage != nullptr) {
                *errorMessage = loadError;
            }
            trayStatusOverride_ = QStringLiteral("Realtime load failed");
            updateTrayStatus();
            engineSwitchInProgress_ = false;
            setOverlayRuntimeControlsEnabled(true);
            return false;
        }
        trayStatusOverride_.clear();
        updateTrayStatus();
    }

    engineSwitchInProgress_ = false;
    setOverlayRuntimeControlsEnabled(true);

    qInfo().noquote() << "language switched from" << previousLanguage << "to" << nextLanguage
                      << (wasRecording ? "(restart session)" : "(idle)");

    if (wasRecording) {
        OutputMode mode = resumeMode;
        if (settings_.isRealtimeEngine() && mode == OutputMode::English) {
            mode = OutputMode::Original;
        }
        startRecording(mode, resumeWindow);
        return true;
    }

    updateWakeWordListening();
    updateTrayStatus();
    return true;
}

void SpeakToComputerApp::showPreferences()
{
    if (state_ != State::Idle) {
        overlay_.showError(QStringLiteral("Preferences can only be changed while the app is idle."),
                QStringLiteral("Preferences"));
        return;
    }

    stopWakeWordListening();

    PreferencesDialog dialog(settings_);
    dialog.setWindowIcon(trayIcon_.icon());
    if (dialog.exec() != QDialog::Accepted) {
        updateWakeWordListening();
        updateTrayStatus();
        return;
    }

    QString errorMessage;
    if (!applySettings(dialog.editedSettings(), &errorMessage)) {
        qWarning().noquote() << errorMessage;
        overlay_.showError(errorMessage, QStringLiteral("Preferences"));
        updateWakeWordListening();
        updateTrayStatus();
        return;
    }

    trayStatusOverride_.clear();
    overlay_.showDone(QStringLiteral("Preferences saved"));
    updateWakeWordListening();
    updateTrayStatus();
    QTimer::singleShot(900, &overlay_, &OverlayWidget::hide);
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
    updateOverlayVadControl();
    if (state_ == State::Recording) {
        configureVadForCurrentRecording();
    }
    updateTrayStatus();
}

void SpeakToComputerApp::handleVadPresetSelected(bool enabled, int endSilenceMs)
{
    if (enabled && endSilenceMs <= 0) {
        qWarning() << "Ignoring invalid VAD auto-stop preset:" << endSilenceMs;
        return;
    }

    if (enabled) {
        QString saveSilenceError;
        if (!AppSettings::saveVadEndSilenceMs(settings_.settingsPath, endSilenceMs, &saveSilenceError)) {
            qWarning().noquote() << saveSilenceError;
        }
        settings_.vadEndSilenceMs = endSilenceMs;
    }

    QString saveToggleError;
    if (!AppSettings::saveVadAutostopEnabled(settings_.settingsPath, enabled, &saveToggleError)) {
        qWarning().noquote() << saveToggleError;
    }
    settings_.vadAutostopEnabled = enabled;
    refreshVadRuntimeStatus();
    updateOverlayVadControl();
    if (state_ == State::Recording) {
        configureVadForCurrentRecording();
    }
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
    QString runtimeError;
    vadRuntimeAvailable_ = WebRtcVad::isRuntimeAvailable(&runtimeError);
    vadRuntimeError_ = runtimeError;
}

void SpeakToComputerApp::updateOverlayVadControl()
{
    overlay_.setVadAutostopPreset(settings_.vadAutostopEnabled, settings_.vadEndSilenceMs);
    // VAD only applies to batch recording — hide the chip in Live to keep overlay compact.
    const bool showVad = vadRuntimeAvailable_ && !settings_.isRealtimeEngine();
    overlay_.setVadControlAvailable(showVad);
}

void SpeakToComputerApp::configureVadForCurrentRecording()
{
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();
    if (!settings_.vadAutostopEnabled) {
        return;
    }
    if (!vadRuntimeAvailable_) {
        qWarning().noquote() << "VAD auto-stop unavailable:" << vadRuntimeError_;
        return;
    }

    const VadEndpointConfig vadConfig{
            settings_.vadAggressiveness,
            settings_.vadEndSilenceMs,
            settings_.vadMinSpeechMs,
    };
    QString vadErrorMessage;
    if (vadEndpointDetector_.reset(vadConfig, &vadErrorMessage)) {
        vadEnabledForCurrentRecording_ = true;
        return;
    }

    qWarning().noquote() << "VAD auto-stop disabled:" << vadErrorMessage;
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

    if (settings_.isBuzzAsrEngine()) {
        if (!QFileInfo::exists(settings_.buzzAsrPython) || !QFileInfo(settings_.buzzAsrPython).isExecutable()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("BuzzASR Python missing: %1\nRun scripts/install-buzzasr-runtime.sh to install the runtime.")
                        .arg(settings_.buzzAsrPython);
            }
            return false;
        }
        if (!QFileInfo::exists(settings_.buzzAsrScript)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("BuzzASR script missing: %1").arg(settings_.buzzAsrScript);
            }
            return false;
        }
        return true;
    }

    if (settings_.isRealtimeEngine()) {
        if (QStandardPaths::findExecutable(QStringLiteral("xclip")).isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("xclip was not found in PATH (required for Realtime paste).");
            }
            return false;
        }
        if (!QFileInfo::exists(settings_.realtimePython) || !QFileInfo(settings_.realtimePython).isExecutable()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Realtime Python missing: %1\nRun scripts/simulstreaming-poc/setup.sh")
                                        .arg(settings_.realtimePython);
            }
            return false;
        }
        if (!QFileInfo::exists(settings_.realtimeServerScript)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Realtime server script missing: %1").arg(settings_.realtimeServerScript);
            }
            return false;
        }
        return true;
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

    if (settings_.isRealtimeEngine()) {
        QString modelName = selectedModelPath;
        if (modelName.startsWith(QStringLiteral("realtime:"))) {
            modelName = modelName.mid(QStringLiteral("realtime:").size());
        }
        if (modelName == settings_.realtimeModel) {
            if (errorMessage != nullptr) {
                errorMessage->clear();
            }
            return false;
        }
        if (!AppSettings::saveRealtimeModel(settings_.settingsPath, modelName, errorMessage)) {
            return false;
        }
        settings_.realtimeModel = modelName;
        refreshModelUi();
        // Reload server with new model (frees previous GPU weights).
        unloadRealtimeServer();
        QString loadError;
        if (!ensureRealtimeServerLoaded(&loadError)) {
            if (errorMessage != nullptr) {
                *errorMessage = loadError;
            }
            return false;
        }
        qInfo().noquote() << "selected realtime model" << settings_.realtimeModel;
        return true;
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
    refreshModelUi();
    qInfo().noquote() << "selected whisper model" << settings_.model;
    return true;
}

void SpeakToComputerApp::handleMiniOverlayToggled(bool enabled)
{
    QString errorMessage;
    if (!AppSettings::saveMiniOverlayEnabled(settings_.settingsPath, enabled, &errorMessage)) {
        const QSignalBlocker blocker(trayMiniOverlayAction_);
        trayMiniOverlayAction_->setChecked(settings_.miniOverlayEnabled);
        overlay_.showError(errorMessage);
        return;
    }
    settings_.miniOverlayEnabled = enabled;
    overlay_.setMiniModeEnabled(enabled);
}

bool SpeakToComputerApp::applySettings(const AppSettings &newSettings, QString *errorMessage)
{
    QString hotkeyError;
    if (!dictateHotkey_.registerHotkey(newSettings.hotkeyDictate, &hotkeyError)) {
        if (!dictateHotkey_.registerHotkey(settings_.hotkeyDictate, nullptr)) {
            qWarning() << "Failed to restore dictation hotkey after preferences error";
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not register dictation hotkey %1: %2")
                                    .arg(newSettings.hotkeyDictate, hotkeyError);
        }
        return false;
    }

    QString translateHotkeyError;
    if (!translateHotkey_.registerHotkey(newSettings.hotkeyTranslateEn, &translateHotkeyError)) {
        dictateHotkey_.registerHotkey(settings_.hotkeyDictate, nullptr);
        translateHotkey_.registerHotkey(settings_.hotkeyTranslateEn, nullptr);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not register English hotkey %1: %2")
                                    .arg(newSettings.hotkeyTranslateEn, translateHotkeyError);
        }
        return false;
    }

    if (!AppSettings::save(newSettings, errorMessage)) {
        dictateHotkey_.registerHotkey(settings_.hotkeyDictate, nullptr);
        translateHotkey_.registerHotkey(settings_.hotkeyTranslateEn, nullptr);
        return false;
    }

    const QString previousEngine = settings_.engine;
    const QString previousRealtimeModel = settings_.realtimeModel;
    settings_ = AppSettings::loadFromPath(newSettings.settingsPath);
    overlay_.setMiniModeEnabled(settings_.miniOverlayEnabled);
    wakeWordAvailable_ = true;
    trayStatusOverride_.clear();
    refreshModelUi();
    refreshEngineUi();
    refreshLanguageUi();
    refreshSendKeywordUi();
    refreshPasteTargetUi();
    realtime_.setPasteTarget(settings_.pasteTarget);
    refreshVadRuntimeStatus();
    updateOverlayVadControl();

    // Always unload when leaving realtime or changing realtime model; load only if realtime selected.
    const bool engineChanged = previousEngine != settings_.engine;
    const bool realtimeModelChanged = previousRealtimeModel != settings_.realtimeModel;
    if (engineChanged || (settings_.isRealtimeEngine() && realtimeModelChanged) || settings_.isWhisperCppEngine()) {
        unloadRealtimeServer();
    }
    if (settings_.isRealtimeEngine()) {
        trayStatusOverride_ = QStringLiteral("Loading Realtime model…");
        updateTrayStatus();
        QString loadError;
        if (!ensureRealtimeServerLoaded(&loadError)) {
            if (errorMessage != nullptr) {
                *errorMessage = loadError;
            }
            trayStatusOverride_ = QStringLiteral("Realtime load failed");
            updateTrayStatus();
            return false;
        }
        trayStatusOverride_.clear();
    }

    qInfo().noquote() << "preferences updated from" << settings_.settingsPath
                      << "engine" << settings_.engine;
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

QString SpeakToComputerApp::gpuMemoryLabel() const
{
    QProcess nvidiaSmi;
    nvidiaSmi.start(QStringLiteral("nvidia-smi"),
            {QStringLiteral("--query-gpu=memory.used,memory.total"),
                    QStringLiteral("--format=csv,noheader,nounits")});
    if (!nvidiaSmi.waitForFinished(500) || nvidiaSmi.exitCode() != 0) {
        return QString();
    }
    const QStringList fields = QString::fromUtf8(nvidiaSmi.readAllStandardOutput())
                                       .trimmed()
                                       .split(QLatin1Char(','));
    if (fields.size() < 2) {
        return QString();
    }
    bool usedOk = false;
    bool totalOk = false;
    const int usedMb = fields.at(0).trimmed().toInt(&usedOk);
    const int totalMb = fields.at(1).trimmed().toInt(&totalOk);
    if (!usedOk || !totalOk || totalMb <= 0) {
        return QString();
    }
    return QStringLiteral("GPU %1/%2 MB").arg(usedMb).arg(totalMb);
}

QString SpeakToComputerApp::outputLabel(OutputMode outputMode) const
{
    const QString gpu = gpuMemoryLabel();
    const QString base = outputMode == OutputMode::English
            ? QStringLiteral("English")
            : QStringLiteral("Original (%1)").arg(originalLanguageLabel());
    return gpu.isEmpty() ? base : QStringLiteral("%1 · %2").arg(base, gpu);
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
    // One compact line — overlay keeps only the first secondary hint.
    return {
            QStringLiteral("%1 stop · %2 EN")
                    .arg(hotkeyFor(OutputMode::Original), hotkeyFor(OutputMode::English)),
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
        if (settings_.isRealtimeEngine()) {
            return QStringLiteral("Realtime live. Press %1 to stop.")
                    .arg(hotkeyFor(currentOutputMode_));
        }
        return QStringLiteral("Recording %1. Press %2 to finish.")
                .arg(outputLabel(currentOutputMode_), hotkeyFor(currentOutputMode_));
    }
    if (state_ == State::Transcribing) {
        return QStringLiteral("Transcribing %1.").arg(outputLabel(currentOutputMode_));
    }
    if (settings_.isRealtimeEngine()) {
        return QStringLiteral("Engine Realtime (%1). %2 starts live paste.")
                .arg(AppSettings::realtimeModelLabel(settings_.realtimeModel), settings_.hotkeyDictate);
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
    const QIcon icon(QStringLiteral(":/icons/speak-to-computer.svg"));
    QApplication::setWindowIcon(icon);
    overlay_.setWindowIcon(icon);

    trayStatusAction_ = trayMenu_.addAction(trayStatusText());
    trayStatusAction_->setEnabled(false);
    trayPreferencesAction_ = trayMenu_.addAction(QStringLiteral("Preferences"));
    connect(trayPreferencesAction_, &QAction::triggered, this, &SpeakToComputerApp::showPreferences);
    trayMiniOverlayAction_ = trayMenu_.addAction(QStringLiteral("Tryb mini"));
    trayMiniOverlayAction_->setCheckable(true);
    trayMiniOverlayAction_->setChecked(settings_.miniOverlayEnabled);
    connect(trayMiniOverlayAction_, &QAction::toggled, this, &SpeakToComputerApp::handleMiniOverlayToggled);
    trayCopyLastTextAction_ = trayMenu_.addAction(QStringLiteral("Kopiuj ostatni tekst"));
    trayCopyLastTextAction_->setEnabled(false);
    connect(trayCopyLastTextAction_, &QAction::triggered, this, [this]() {
        QString errorMessage;
        if (!paster_.copy(lastTranscription_, &errorMessage)) {
            overlay_.showError(errorMessage);
            return;
        }
        trayIcon_.showMessage(QStringLiteral("Speak to Computer"), QStringLiteral("Ostatni tekst skopiowany."));
    });
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

void SpeakToComputerApp::setOverlayCloseHotkeyEnabled(bool enabled)
{
    if (overlayCloseHotkeyEnabled_ == enabled) {
        return;
    }

    if (!enabled) {
        overlayCloseHotkey_.unregisterHotkey();
        overlayCloseHotkeyEnabled_ = false;
        return;
    }

    QString errorMessage;
    // Overlay does not take focus, so close must be a global grab while visible.
    // Shift+Esc avoids accidental cancel from bare Escape during dictation.
    if (!overlayCloseHotkey_.registerHotkey(QStringLiteral("Shift+Escape"), &errorMessage)) {
        qWarning().noquote() << "Could not register overlay close hotkey Shift+Escape:" << errorMessage;
        return;
    }

    overlayCloseHotkeyEnabled_ = true;
}

void SpeakToComputerApp::updateTrayStatus()
{
    if (state_ != State::Recording) {
        mouseFinishHotkey_.unregisterHotkey();
    }
    if (trayMiniOverlayAction_ != nullptr && trayMiniOverlayAction_->isChecked() != settings_.miniOverlayEnabled) {
        const QSignalBlocker blocker(trayMiniOverlayAction_);
        trayMiniOverlayAction_->setChecked(settings_.miniOverlayEnabled);
    }
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
    setOverlayRuntimeControlsEnabled(true);
    // Empty result is one sentence. 15s felt like the window never closed.
    overlay_.showError(message, QStringLiteral("Dictation alert"), 2500);
    updateWakeWordListening();
    updateTrayStatus();
}

void SpeakToComputerApp::showErrorAndReturnIdle(const QString &message)
{
    if (realtime_.isSessionActive()) {
        realtime_.stopSession();
    }
    state_ = State::Idle;
    recordingStopRequested_ = false;
    vadEnabledForCurrentRecording_ = false;
    vadEndpointDetector_.clear();
    trayStatusOverride_ = QStringLiteral("Error: %1").arg(message);
    setOverlayRuntimeControlsEnabled(true);
    overlay_.showError(message);
    updateWakeWordListening();
    updateTrayStatus();
}

void SpeakToComputerApp::quitApplication()
{
    stopWakeWordListening();
    if (state_ == State::Recording) {
        cancelRecording();
    }
    unloadRealtimeServer();
    BuzzAsrRunner::killOrphanedDaemons();
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
    // -loglevel quiet: avoid dumping wav metadata/stats into the tray console.
    QProcess::startDetached(QStringLiteral("ffplay"),
            {QStringLiteral("-nodisp"),
                    QStringLiteral("-autoexit"),
                    QStringLiteral("-loglevel"),
                    QStringLiteral("quiet"),
                    QStringLiteral("-volume"),
                    QStringLiteral("50"),
                    resolvedPath});
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
    QProcess::startDetached(QStringLiteral("ffplay"),
            {QStringLiteral("-nodisp"),
                    QStringLiteral("-autoexit"),
                    QStringLiteral("-loglevel"),
                    QStringLiteral("quiet"),
                    QStringLiteral("-volume"),
                    QStringLiteral("50"),
                    resolvedPath});
}
