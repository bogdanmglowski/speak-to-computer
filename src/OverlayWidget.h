#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <QWidget>

class QPlainTextEdit;
class QResizeEvent;

class OverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit OverlayWidget(QWidget *parent = nullptr);

    void showRecording();
    void showTranscribing();
    void showDone(const QString &message);
    void showError(const QString &message);
    void setAudioLevel(double level);
    void setElapsedMs(qint64 elapsedMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Mode {
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

    Mode mode_ = Mode::Recording;
    QString title_;
    QString subtitle_;
    QPlainTextEdit *errorText_ = nullptr;
    double audioLevel_ = 0.0;
    qint64 elapsedMs_ = 0;
};
