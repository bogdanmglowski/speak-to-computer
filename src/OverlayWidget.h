#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <QStringList>
#include <QWidget>


class QMouseEvent;
class QPainter;
class QPlainTextEdit;
class QHideEvent;
class QKeyEvent;
class QResizeEvent;
class QRectF;

class OverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit OverlayWidget(QWidget *parent = nullptr);

    void showRecording(const QString &outputLabel, const QStringList &hints);
    void showListening(const QString &wakeWordPhrase);
    void showTranscribing(const QString &outputLabel);
    void showDone(const QString &message);
    void showError(const QString &message);
    void showError(const QString &message, const QString &title);
    void showError(const QString &message, const QString &title, int autoHideMs);
    void setAudioLevel(double level);
    void setElapsedMs(qint64 elapsedMs);
    void setGpuMemoryLabel(const QString &label);
    void setMiniModeEnabled(bool enabled);
    void setModelLabel(const QString &label);
    void setModelControlEnabled(bool enabled);
    void setAvailableModelPaths(const QStringList &modelPaths);
    void setEngineLabel(const QString &label);
    void setEngineControlEnabled(bool enabled);
    void setLanguageLabel(const QString &label);
    void setLanguageControlEnabled(bool enabled);
    void setSendKeyword(const QString &keyword, bool enabled);
    void setSendKeywordControlEnabled(bool enabled);
    void setPasteTargetLabel(const QString &label);
    void setPasteTargetControlEnabled(bool enabled);
    void setVadAutostopPreset(bool enabled, int endSilenceMs);
    void setVadControlAvailable(bool available);
    bool isRecordingMode() const;

signals:
    void modelSelected(const QString &modelPath);
    void engineSelected(const QString &engineId);
    void languageSelected(const QString &languageCode);
    void pasteTargetSelected(const QString &pasteTargetId);
    void sendKeywordToggled(bool enabled);
    void vadPresetSelected(bool enabled, int endSilenceMs);
    void closeRequested();
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Mode {
        Listening,
        Recording,
        Transcribing,
        Done,
        Error,
    };

    void updateWindowSize();
    void drawVoiceVisualizer(QPainter &painter, const QRectF &bounds) const;
    QSize errorWindowSize() const;
    void updateErrorTextGeometry();
    void placeOnPrimaryScreen();
    QString elapsedText() const;
    QRectF modelChipRect(const QRectF &card) const;
    QRectF engineChipRect(const QRectF &card) const;
    QRectF languageChipRect(const QRectF &card) const;
    QRectF pasteTargetChipRect(const QRectF &card) const;
    QRectF sendKeywordChipRect(const QRectF &card) const;
    QRectF closeButtonRect(const QRectF &card) const;
    QRectF vadChipRect(const QRectF &card) const;
    QString vadChipLabel() const;
    QString sendKeywordChipLabel() const;
    void showModelMenu(const QPoint &globalPos);
    void showEngineMenu(const QPoint &globalPos);
    void showLanguageMenu(const QPoint &globalPos);
    void showPasteTargetMenu(const QPoint &globalPos);
    void showVadMenu(const QPoint &globalPos);

    Mode mode_ = Mode::Recording;
    QString title_;
    QString subtitle_;
    QStringList secondarySubtitles_;
    QString modelLabel_ = QStringLiteral("Small");
    QString engineLabel_ = QStringLiteral("Batch");
    QString languageLabel_ = QStringLiteral("pl");
    QString pasteTargetLabel_ = QStringLiteral("Pin");
    QString sendKeyword_ = QStringLiteral("go");
    QStringList availableModelPaths_;
    QPlainTextEdit *errorText_ = nullptr;
    double audioLevel_ = 0.0;
    qint64 elapsedMs_ = 0;
    QString gpuMemoryLabel_;
    int errorDisplayId_ = 0;
    bool modelControlEnabled_ = true;
    bool engineControlEnabled_ = true;
    bool languageControlEnabled_ = true;
    bool pasteTargetControlEnabled_ = true;
    bool sendKeywordEnabled_ = true;
    bool sendKeywordControlEnabled_ = true;
    bool vadAutostopEnabled_ = false;
    int vadEndSilenceMs_ = 900;
    bool vadControlAvailable_ = true;
    bool miniModeEnabled_ = false;
};
