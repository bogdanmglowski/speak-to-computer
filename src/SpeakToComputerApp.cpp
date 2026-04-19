#include "SpeakToComputerApp.h"

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

        const QRectF background(4.0 * unit, 4.0 * unit, 56.0 * unit, 56.0 * unit);
        painter.setPen(QPen(QColor(255, 255, 255, 55), 2.0 * unit));
        painter.setBrush(QColor(22, 24, 28));
        painter.drawRoundedRect(background, 12.0 * unit, 12.0 * unit);

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

} // namespace

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

    setupTrayIcon();
}

bool SpeakToComputerApp::start()
{
    QString errorMessage;
    if (!hotkey_.registerHotkey(settings_.hotkey, &errorMessage)) {
        qWarning().noquote() << errorMessage;
        overlay_.showError(errorMessage);
        trayStatusOverride_ = QStringLiteral("Error: %1").arg(errorMessage);
        updateTrayStatus();
        return false;
    }
    qInfo().noquote() << "speak-to-computer listening for" << settings_.hotkey;
    qInfo().noquote() << "settings:" << settings_.settingsPath;
    trayStatusOverride_.clear();
    updateTrayStatus();
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
    trayStatusOverride_.clear();
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
    updateTrayStatus();
    qInfo().noquote() << "recording started using audio backend" << recorder_.activeBackendName();
}

void SpeakToComputerApp::stopRecording()
{
    trayStatusOverride_.clear();
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
    updateTrayStatus();
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
    trayStatusOverride_.clear();
    overlay_.setModelControlEnabled(true);
    overlay_.showDone(QStringLiteral("Text pasted into the active window"));
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
    overlay_.showDone(QStringLiteral("Switched to %1. Press hotkey to retry.").arg(fallbackLabel));
    updateTrayStatus();
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

QString SpeakToComputerApp::trayStatusText() const
{
    if (!trayStatusOverride_.isEmpty()) {
        return trayStatusOverride_;
    }

    if (state_ == State::Recording) {
        return QStringLiteral("Recording. Press %1 to finish.").arg(settings_.hotkey);
    }
    if (state_ == State::Transcribing) {
        return QStringLiteral("Transcribing.");
    }
    return QStringLiteral("Ready. Press %1 to dictate.").arg(settings_.hotkey);
}

void SpeakToComputerApp::setupTrayIcon()
{
    const QIcon icon = createApplicationIcon();
    QApplication::setWindowIcon(icon);
    overlay_.setWindowIcon(icon);

    trayStatusAction_ = trayMenu_.addAction(trayStatusText());
    trayStatusAction_->setEnabled(false);
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
    const QString status = trayStatusText();
    if (trayStatusAction_ != nullptr) {
        trayStatusAction_->setText(status);
    }
    trayIcon_.setToolTip(QStringLiteral("Speak to Computer\n%1").arg(status));
}

void SpeakToComputerApp::showErrorAndReturnIdle(const QString &message)
{
    state_ = State::Idle;
    trayStatusOverride_ = QStringLiteral("Error: %1").arg(message);
    overlay_.setModelControlEnabled(true);
    overlay_.showError(message);
    updateTrayStatus();
}

void SpeakToComputerApp::quitApplication()
{
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
