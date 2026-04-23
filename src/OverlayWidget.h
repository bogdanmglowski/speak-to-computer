#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <QStringList>
#include <QWidget>

class QMouseEvent;
class QPlainTextEdit;
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
    void setAudioLevel(double level);
    void setElapsedMs(qint64 elapsedMs);
    void setModelLabel(const QString &label);
    void setModelControlEnabled(bool enabled);
    void setAvailableModelPaths(const QStringList &modelPaths);
    void setVadAutostopPreset(bool enabled, int endSilenceMs);
    void setVadControlAvailable(bool available);

signals:
    void modelSelected(const QString &modelPath);
    void vadPresetSelected(bool enabled, int endSilenceMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
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
    QSize errorWindowSize() const;
    void updateErrorTextGeometry();
    void placeOnPrimaryScreen();
    QString elapsedText() const;
    QRectF modelChipRect(const QRectF &card) const;
    QRectF vadChipRect(const QRectF &card) const;
    QString vadChipLabel() const;
    void showModelMenu(const QPoint &globalPos);
    void showVadMenu(const QPoint &globalPos);

    Mode mode_ = Mode::Recording;
    QString title_;
    QString subtitle_;
    QStringList secondarySubtitles_;
    QString modelLabel_ = QStringLiteral("Small");
    QStringList availableModelPaths_;
    QPlainTextEdit *errorText_ = nullptr;
    double audioLevel_ = 0.0;
    qint64 elapsedMs_ = 0;
    int errorDisplayId_ = 0;
    bool modelControlEnabled_ = true;
    bool vadAutostopEnabled_ = false;
    int vadEndSilenceMs_ = 900;
    bool vadControlAvailable_ = true;
};
