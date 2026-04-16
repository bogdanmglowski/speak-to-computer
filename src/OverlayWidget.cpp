#include "OverlayWidget.h"

#include <QApplication>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTextDocument>
#include <QTextOption>

#include <algorithm>

namespace {

constexpr int defaultWidth = 380;
constexpr int defaultHeight = 112;
constexpr int outerMargin = 8;
constexpr int textLeft = 58;
constexpr int rightMargin = 24;
constexpr int titleTop = 19;
constexpr int titleHeight = 24;
constexpr int bodyTop = 48;
constexpr int cardBottomMargin = 20;

} // namespace

OverlayWidget::OverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
            | Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    errorText_ = new QPlainTextEdit(this);
    QFont bodyFont = font();
    bodyFont.setPointSize(10);
    errorText_->setFont(bodyFont);
    errorText_->setReadOnly(true);
    errorText_->setFrameShape(QFrame::NoFrame);
    errorText_->setFocusPolicy(Qt::NoFocus);
    errorText_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    errorText_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    errorText_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    errorText_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    errorText_->document()->setDocumentMargin(0);
    errorText_->viewport()->setAutoFillBackground(false);
    errorText_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background: transparent; color: rgb(194, 200, 210); border: none; padding: 0px; }"
            "QScrollBar:vertical { width: 8px; background: rgba(255, 255, 255, 22); border-radius: 4px; }"
            "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 78); border-radius: 4px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"));
    errorText_->hide();

    setFixedSize(defaultWidth, defaultHeight);
}

void OverlayWidget::showRecording()
{
    mode_ = Mode::Recording;
    title_ = QStringLiteral("Recording");
    subtitle_ = QStringLiteral("Press the hotkey again to finish");
    audioLevel_ = 0.0;
    elapsedMs_ = 0;
    errorText_->hide();
    updateWindowSize();
    placeOnPrimaryScreen();
    show();
    raise();
    update();
}

void OverlayWidget::showTranscribing()
{
    mode_ = Mode::Transcribing;
    title_ = QStringLiteral("Transcribing");
    subtitle_ = QStringLiteral("Whisper is processing the recording");
    audioLevel_ = 0.0;
    errorText_->hide();
    updateWindowSize();
    placeOnPrimaryScreen();
    show();
    raise();
    update();
}

void OverlayWidget::showDone(const QString &message)
{
    mode_ = Mode::Done;
    title_ = QStringLiteral("Inserted");
    subtitle_ = message;
    audioLevel_ = 1.0;
    errorText_->hide();
    updateWindowSize();
    placeOnPrimaryScreen();
    show();
    raise();
    update();
}

void OverlayWidget::showError(const QString &message)
{
    mode_ = Mode::Error;
    title_ = QStringLiteral("Dictation error");
    subtitle_ = message;
    audioLevel_ = 0.0;
    errorText_->setPlainText(message);
    updateWindowSize();
    errorText_->show();
    placeOnPrimaryScreen();
    show();
    raise();
    update();
}

void OverlayWidget::setAudioLevel(double level)
{
    audioLevel_ = std::clamp(level, 0.0, 1.0);
    update();
}

void OverlayWidget::setElapsedMs(qint64 elapsedMs)
{
    elapsedMs_ = elapsedMs;
    if (mode_ == Mode::Recording) {
        update();
    }
}

void OverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = rect().adjusted(8, 8, -8, -8);
    painter.setPen(QPen(QColor(255, 255, 255, 45), 1));
    painter.setBrush(QColor(22, 24, 28, 236));
    painter.drawRoundedRect(card, 8, 8);

    QColor accent(70, 190, 130);
    if (mode_ == Mode::Recording) {
        accent = QColor(229, 70, 78);
    } else if (mode_ == Mode::Transcribing) {
        accent = QColor(80, 155, 230);
    } else if (mode_ == Mode::Error) {
        accent = QColor(240, 170, 60);
    }

    const QPointF indicatorCenter(card.left() + 34, card.top() + 36);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(indicatorCenter, 9, 9);
    if (mode_ == Mode::Recording) {
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 55));
        painter.drawEllipse(indicatorCenter, 17, 17);
    }

    QFont titleFont = font();
    titleFont.setPointSize(13);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(245, 247, 250));
    painter.drawText(QRectF(card.left() + textLeft, card.top() + titleTop, card.width() - 82, titleHeight),
            title_);

    if (mode_ == Mode::Error) {
        return;
    }

    QFont bodyFont = font();
    bodyFont.setPointSize(10);
    painter.setFont(bodyFont);
    painter.setPen(QColor(194, 200, 210));
    QString body = subtitle_;
    const QFontMetrics metrics(bodyFont);
    body = metrics.elidedText(body, Qt::ElideRight, static_cast<int>(card.width() - 82));
    painter.drawText(QRectF(card.left() + textLeft, card.top() + bodyTop, card.width() - 82, 22), body);

    const QRectF bar(card.left() + textLeft, card.bottom() - 24, card.width() - 114, 7);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 38));
    painter.drawRoundedRect(bar, 4, 4);
    const QRectF fill(bar.left(), bar.top(), bar.width() * audioLevel_, bar.height());
    painter.setBrush(accent);
    painter.drawRoundedRect(fill, 4, 4);

    painter.setFont(bodyFont);
    painter.setPen(QColor(194, 200, 210));
    painter.drawText(QRectF(bar.right() + 12, bar.top() - 6, 50, 18), Qt::AlignLeft, elapsedText());
}

void OverlayWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateErrorTextGeometry();
}

void OverlayWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    placeOnPrimaryScreen();
}

void OverlayWidget::updateWindowSize()
{
    if (mode_ == Mode::Error) {
        setFixedSize(errorWindowSize());
    } else {
        setFixedSize(defaultWidth, defaultHeight);
    }
    updateErrorTextGeometry();
}

QSize OverlayWidget::errorWindowSize() const
{
    const QRect available = QGuiApplication::primaryScreen() == nullptr
            ? QRect(0, 0, 800, 600)
            : QGuiApplication::primaryScreen()->availableGeometry();
    const int maxWidth = std::max(300, std::min(760, available.width() - 48));
    const int minWidth = std::min(defaultWidth, maxWidth);
    const int maxHeight = std::max(128, std::min(420, available.height() - 96));

    QFont bodyFont = font();
    bodyFont.setPointSize(10);
    const QFontMetrics metrics(bodyFont);

    const int horizontalChrome = outerMargin * 2 + textLeft + rightMargin;
    const int desiredWidth = std::clamp(metrics.horizontalAdvance(subtitle_) + horizontalChrome, minWidth, maxWidth);
    const int bodyWidth = std::max(160, desiredWidth - horizontalChrome);
    const QRect wrapped = metrics.boundingRect(QRect(0, 0, bodyWidth, 10000), Qt::TextWordWrap, subtitle_);
    const int desiredHeight = outerMargin * 2 + bodyTop + wrapped.height() + cardBottomMargin;

    return QSize(desiredWidth, std::clamp(desiredHeight, 128, maxHeight));
}

void OverlayWidget::updateErrorTextGeometry()
{
    if (errorText_ == nullptr || mode_ != Mode::Error) {
        return;
    }

    const QRect card = rect().adjusted(outerMargin, outerMargin, -outerMargin, -outerMargin);
    const int textX = card.left() + textLeft;
    const int textY = card.top() + bodyTop;
    const int textWidth = card.width() - textLeft - rightMargin;
    const int textHeight = card.height() - bodyTop - cardBottomMargin;
    errorText_->setGeometry(textX, textY, textWidth, std::max(32, textHeight));
}

void OverlayWidget::placeOnPrimaryScreen()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }

    const QRect available = screen->availableGeometry();
    const int x = available.center().x() - width() / 2;
    const int y = available.top() + 56;
    move(x, y);
}

QString OverlayWidget::elapsedText() const
{
    if (mode_ != Mode::Recording) {
        return QStringLiteral("");
    }

    const qint64 totalSeconds = elapsedMs_ / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}
