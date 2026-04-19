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

    void showRecording();
    void showTranscribing();
    void showDone(const QString &message);
    void showError(const QString &message);
    void setAudioLevel(double level);
    void setElapsedMs(qint64 elapsedMs);
    void setModelLabel(const QString &label);
    void setModelControlEnabled(bool enabled);
    void setAvailableModelPaths(const QStringList &modelPaths);

signals:
    void modelSelected(const QString &modelPath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
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
    QRectF modelChipRect(const QRectF &card) const;
    void showModelMenu(const QPoint &globalPos);

    Mode mode_ = Mode::Recording;
    QString title_;
    QString subtitle_;
    QString modelLabel_ = QStringLiteral("Small");
    QStringList availableModelPaths_;
    QPlainTextEdit *errorText_ = nullptr;
    double audioLevel_ = 0.0;
    qint64 elapsedMs_ = 0;
    bool modelControlEnabled_ = true;
};
