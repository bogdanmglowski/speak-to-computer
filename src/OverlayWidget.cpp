#include "OverlayWidget.h"

#include "AppSettings.h"

#include <QApplication>
#include <QAction>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QTextDocument>
#include <QTextOption>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

constexpr int defaultWidth = 540;
constexpr int outerMargin = 8;
constexpr int textLeft = 52;
constexpr int rightMargin = 16;
constexpr int titleTop = 14;
constexpr int titleHeight = 22;
constexpr int bodyTop = 46;
constexpr int bodyTextPointSize = 11;
constexpr int bodyTextHeight = 18;
constexpr int bodyLineGap = 4;
constexpr int textToBarGap = 10;
constexpr int progressBarTopFromCardBottom = 16;
constexpr int waveformHeightPx = 32;
constexpr std::size_t waveformPointCount = 65;
constexpr int waveformPeakCount = 5;
constexpr int miniWidthPx = 380;
constexpr int miniHeightPx = 56;
constexpr int miniPaddingPx = 12;
constexpr int cardBottomMargin = 12;
constexpr int modelChipTop = 11;
constexpr int modelChipHeight = 26;
constexpr int modelChipRightPadding = 12;
constexpr int modelChipHorizontalPadding = 8;
constexpr int modelChipArrowWidth = 9;
constexpr int modelChipGapToTitle = 8;
constexpr int chipGap = 4;
constexpr int closeButtonWidth = 22;
constexpr int closeButtonGap = 5;
// Left-side send-keyword chip sits after the status indicator.
constexpr int sendKeywordLeft = 50;
constexpr int vadChipHeight = 24;
constexpr int vadChipHorizontalPadding = 8;
constexpr int vadChipArrowWidth = 9;
constexpr int vadChipMinWidth = 88;
constexpr int errorAutoHideMs = 15000;
constexpr int maxSecondaryHints = 1;

constexpr std::array<int, 5> vadPresetValues = {300, 500, 900, 1500, 2500};

int windowHeightForBodyLines(int lineCount)
{
    const int safeLineCount = std::max(1, lineCount);
    const int textBlockHeight = safeLineCount * bodyTextHeight + (safeLineCount - 1) * bodyLineGap;
    return outerMargin * 2 + bodyTop + textBlockHeight + textToBarGap + progressBarTopFromCardBottom;
}

} // namespace

OverlayWidget::OverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
            | Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);

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

    setFixedSize(defaultWidth, windowHeightForBodyLines(1));
}

void OverlayWidget::showRecording(const QString &outputLabel, const QStringList &hints)
{
    mode_ = Mode::Recording;
    title_ = QStringLiteral("Recording");
    // Compact: single primary line, at most one secondary hint (chips carry model/engine).
    subtitle_ = outputLabel.trimmed().isEmpty() ? QStringLiteral("Dictation") : outputLabel.trimmed();
    secondarySubtitles_.clear();
    for (const QString &hint : hints) {
        if (hint.trimmed().isEmpty()) {
            continue;
        }
        secondarySubtitles_ << hint.trimmed();
        if (secondarySubtitles_.size() >= maxSecondaryHints) {
            break;
        }
    }
    audioLevel_ = 0.0;
    elapsedMs_ = 0;
    errorText_->hide();
    updateWindowSize();
    placeOnPrimaryScreen();
    show();
    raise();
    update();
}

void OverlayWidget::showListening(const QString &wakeWordPhrase)
{
    mode_ = Mode::Listening;
    title_ = QStringLiteral("Listening");
    subtitle_ = QStringLiteral("Wake word: %1").arg(wakeWordPhrase);
    secondarySubtitles_ = QStringList{QStringLiteral("Say the wake word to start recording")};
    audioLevel_ = 0.0;
    elapsedMs_ = 0;
    errorText_->hide();
    updateWindowSize();
    placeOnPrimaryScreen();
    show();
    raise();
    update();
}

void OverlayWidget::showTranscribing(const QString &outputLabel)
{
    mode_ = Mode::Transcribing;
    title_ = QStringLiteral("Transcribing");
    subtitle_ = QStringLiteral("Output: %1").arg(outputLabel);
    secondarySubtitles_ = QStringList{QStringLiteral("Whisper is processing the recording")};
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
    secondarySubtitles_.clear();
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
    showError(message, QStringLiteral("Dictation error"));
}

void OverlayWidget::showError(const QString &message, const QString &title)
{
    showError(message, title, errorAutoHideMs);
}

void OverlayWidget::showError(const QString &message, const QString &title, int autoHideMs)
{
    mode_ = Mode::Error;
    title_ = title;
    subtitle_ = message;
    secondarySubtitles_.clear();
    audioLevel_ = 0.0;
    errorText_->setPlainText(message);
    updateWindowSize();
    errorText_->show();
    placeOnPrimaryScreen();
    show();
    raise();
    update();

    const int displayId = ++errorDisplayId_;
    QTimer::singleShot(std::max(0, autoHideMs), this, [this, displayId]() {
        if (mode_ == Mode::Error && errorDisplayId_ == displayId) {
            hide();
        }
    });
}

void OverlayWidget::setAudioLevel(double level)
{
    audioLevel_ = std::clamp(level, 0.0, 1.0);
    update();
}

void OverlayWidget::setMiniModeEnabled(bool enabled)
{
    miniModeEnabled_ = enabled;
    updateWindowSize();
    if (isVisible()) {
        placeOnPrimaryScreen();
    }
    update();
}

void OverlayWidget::setElapsedMs(qint64 elapsedMs)
{
    elapsedMs_ = elapsedMs;
    if (mode_ == Mode::Recording) {
        update();
    }
}

void OverlayWidget::setGpuMemoryLabel(const QString &label)
{
    if (gpuMemoryLabel_ == label) {
        return;
    }
    gpuMemoryLabel_ = label;
    update();
}

void OverlayWidget::setModelLabel(const QString &label)
{
    if (modelLabel_ == label) {
        return;
    }
    modelLabel_ = label;
    update();
}

void OverlayWidget::setModelControlEnabled(bool enabled)
{
    if (modelControlEnabled_ == enabled) {
        return;
    }
    modelControlEnabled_ = enabled;
    update();
}

void OverlayWidget::setEngineLabel(const QString &label)
{
    if (engineLabel_ == label) {
        return;
    }
    engineLabel_ = label;
    update();
}

void OverlayWidget::setEngineControlEnabled(bool enabled)
{
    if (engineControlEnabled_ == enabled) {
        return;
    }
    engineControlEnabled_ = enabled;
    update();
}

void OverlayWidget::setLanguageLabel(const QString &label)
{
    if (languageLabel_ == label) {
        return;
    }
    languageLabel_ = label;
    update();
}

void OverlayWidget::setLanguageControlEnabled(bool enabled)
{
    if (languageControlEnabled_ == enabled) {
        return;
    }
    languageControlEnabled_ = enabled;
    update();
}

void OverlayWidget::setSendKeyword(const QString &keyword, bool enabled)
{
    const QString cleaned = keyword.trimmed().isEmpty() ? QStringLiteral("go") : keyword.trimmed();
    if (sendKeyword_ == cleaned && sendKeywordEnabled_ == enabled) {
        return;
    }
    sendKeyword_ = cleaned;
    sendKeywordEnabled_ = enabled;
    update();
}

void OverlayWidget::setSendKeywordControlEnabled(bool enabled)
{
    if (sendKeywordControlEnabled_ == enabled) {
        return;
    }
    sendKeywordControlEnabled_ = enabled;
    update();
}

void OverlayWidget::setPasteTargetLabel(const QString &label)
{
    const QString cleaned = label.trimmed().isEmpty() ? QStringLiteral("Pin") : label.trimmed();
    if (pasteTargetLabel_ == cleaned) {
        return;
    }
    pasteTargetLabel_ = cleaned;
    update();
}

void OverlayWidget::setPasteTargetControlEnabled(bool enabled)
{
    if (pasteTargetControlEnabled_ == enabled) {
        return;
    }
    pasteTargetControlEnabled_ = enabled;
    update();
}

void OverlayWidget::setAvailableModelPaths(const QStringList &modelPaths)
{
    // Paths may be absolute ggml files or synthetic "realtime:<name>" entries.

    if (availableModelPaths_ == modelPaths) {
        return;
    }
    availableModelPaths_ = modelPaths;
}

void OverlayWidget::setVadAutostopPreset(bool enabled, int endSilenceMs)
{
    if (vadAutostopEnabled_ == enabled && vadEndSilenceMs_ == endSilenceMs) {
        return;
    }
    vadAutostopEnabled_ = enabled;
    vadEndSilenceMs_ = endSilenceMs;
    if (mode_ == Mode::Recording) {
        update();
    }
}

void OverlayWidget::setVadControlAvailable(bool available)
{
    if (vadControlAvailable_ == available) {
        return;
    }
    vadControlAvailable_ = available;
    if (mode_ == Mode::Recording) {
        updateWindowSize();
        update();
    }
}

bool OverlayWidget::isRecordingMode() const
{
    return mode_ == Mode::Recording;
}

void OverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (miniModeEnabled_ && mode_ == Mode::Recording) {
        QFont miniFont = font();
        miniFont.setPointSize(9);
        painter.setFont(miniFont);
        drawVoiceVisualizer(painter, rect().adjusted(miniPaddingPx, miniPaddingPx, -miniPaddingPx, -miniPaddingPx));
        return;
    }

    const QRectF card = rect().adjusted(8, 8, -8, -8);
    painter.setPen(QPen(QColor(255, 255, 255, 45), 1));
    painter.setBrush(QColor(22, 24, 28, 236));
    painter.drawRoundedRect(card, 8, 8);

    QColor accent(70, 190, 130);
    if (mode_ == Mode::Recording) {
        accent = QColor(229, 70, 78);
    } else if (mode_ == Mode::Listening) {
        accent = QColor(70, 190, 130);
    } else if (mode_ == Mode::Transcribing) {
        accent = QColor(80, 155, 230);
    } else if (mode_ == Mode::Error) {
        accent = QColor(240, 170, 60);
    }

    const QPointF indicatorCenter(card.left() + 28, card.top() + 28);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(indicatorCenter, 7, 7);
    if (mode_ == Mode::Recording) {
        painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 55));
        painter.drawEllipse(indicatorCenter, 13, 13);
    }

    const QRectF sendKeywordRect = sendKeywordChipRect(card);
    const QRectF pasteTargetRect = pasteTargetChipRect(card);
    const QRectF languageRect = languageChipRect(card);
    const QRectF engineRect = engineChipRect(card);
    const QRectF chipRect = modelChipRect(card);

    // Title sits between left chips (go / paste target) and right control chips.
    QFont titleFont = font();
    titleFont.setPointSize(13);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(245, 247, 250));
    const qreal titleLeft = pasteTargetRect.right() + modelChipGapToTitle;
    const qreal titleWidth = std::max<qreal>(72.0, languageRect.left() - titleLeft - modelChipGapToTitle);
    const QFontMetrics titleMetrics(titleFont);
    const QString elidedTitle = titleMetrics.elidedText(title_, Qt::ElideRight, static_cast<int>(titleWidth));
    painter.drawText(QRectF(titleLeft, card.top() + titleTop, titleWidth, titleHeight), elidedTitle);

    auto drawChip = [&](const QRectF &rect, const QString &label, bool enabled) {
        painter.setPen(QPen(QColor(255, 255, 255, enabled ? 44 : 30), 1));
        painter.setBrush(QColor(32, 36, 43, enabled ? 230 : 165));
        painter.drawRoundedRect(rect, 6, 6);

        QFont chipFont = font();
        chipFont.setPointSize(10);
        chipFont.setWeight(QFont::DemiBold);
        painter.setFont(chipFont);
        painter.setPen(QColor(220, 224, 232, enabled ? 255 : 155));
        painter.drawText(rect.adjusted(modelChipHorizontalPadding, 0, -(modelChipArrowWidth + 8), 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                label);

        const qreal arrowX = rect.right() - modelChipArrowWidth;
        const qreal arrowY = rect.center().y() - 2.0;
        QPolygonF arrow = {
                QPointF(arrowX - 4.0, arrowY),
                QPointF(arrowX + 4.0, arrowY),
                QPointF(arrowX, arrowY + 5.0),
        };
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 224, 232, enabled ? 235 : 145));
        painter.drawPolygon(arrow);
    };

    // Send-keyword chip: checkbox-style (filled when on).
    {
        const bool enabled = sendKeywordControlEnabled_;
        const bool on = sendKeywordEnabled_;
        painter.setPen(QPen(QColor(255, 255, 255, enabled ? 44 : 30), 1));
        painter.setBrush(QColor(32, 36, 43, enabled ? 230 : 165));
        painter.drawRoundedRect(sendKeywordRect, 6, 6);

        const qreal box = 12.0;
        const QRectF checkBox(sendKeywordRect.left() + 8.0,
                sendKeywordRect.center().y() - box / 2.0,
                box,
                box);
        painter.setPen(QPen(QColor(220, 224, 232, enabled ? 220 : 120), 1.5));
        painter.setBrush(on ? QColor(70, 190, 130, enabled ? 230 : 140) : Qt::NoBrush);
        painter.drawRoundedRect(checkBox, 3, 3);
        if (on) {
            painter.setPen(QPen(QColor(245, 247, 250, enabled ? 255 : 160), 1.8));
            painter.drawLine(QPointF(checkBox.left() + 2.5, checkBox.center().y()),
                    QPointF(checkBox.center().x() - 0.5, checkBox.bottom() - 3.0));
            painter.drawLine(QPointF(checkBox.center().x() - 0.5, checkBox.bottom() - 3.0),
                    QPointF(checkBox.right() - 2.5, checkBox.top() + 2.5));
        }

        QFont chipFont = font();
        chipFont.setPointSize(10);
        chipFont.setWeight(QFont::DemiBold);
        painter.setFont(chipFont);
        painter.setPen(QColor(220, 224, 232, enabled ? 255 : 155));
        painter.drawText(sendKeywordRect.adjusted(8 + box + 6, 0, -6, 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                sendKeywordChipLabel());
    }

    drawChip(pasteTargetRect, pasteTargetLabel_, pasteTargetControlEnabled_);
    drawChip(languageRect, languageLabel_, languageControlEnabled_);
    drawChip(engineRect, engineLabel_, engineControlEnabled_);
    drawChip(chipRect, modelLabel_, modelControlEnabled_);

    const QRectF closeRect = closeButtonRect(card);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(229, 70, 78));
    painter.drawRoundedRect(closeRect, 6, 6);

    painter.setPen(QPen(QColor(245, 247, 250), 2));
    const qreal closePad = 6;
    painter.drawLine(QPointF(closeRect.left() + closePad, closeRect.top() + closePad),
            QPointF(closeRect.right() - closePad, closeRect.bottom() - closePad));
    painter.drawLine(QPointF(closeRect.right() - closePad, closeRect.top() + closePad),
            QPointF(closeRect.left() + closePad, closeRect.bottom() - closePad));

    if (mode_ == Mode::Error) {
        return;
    }

    QFont bodyFont = font();
    bodyFont.setPointSize(bodyTextPointSize);
    painter.setFont(bodyFont);
    painter.setPen(QColor(194, 200, 210));
    QString body = subtitle_;
    const QFontMetrics metrics(bodyFont);
    const qreal bodyWidth = card.width() - textLeft - rightMargin;
    body = metrics.elidedText(body, Qt::ElideRight, static_cast<int>(bodyWidth));
    painter.drawText(QRectF(card.left() + textLeft, card.top() + bodyTop, bodyWidth, bodyTextHeight), body);

    qreal secondaryTop = card.top() + bodyTop + bodyTextHeight + bodyLineGap;
    if (mode_ == Mode::Recording && vadControlAvailable_) {
        const QRectF vadRect = vadChipRect(card);
        const int vadBorderAlpha = 44;
        const int vadFillAlpha = 230;
        const int vadTextAlpha = 255;

        painter.setPen(QPen(QColor(255, 255, 255, vadBorderAlpha), 1));
        painter.setBrush(QColor(32, 36, 43, vadFillAlpha));
        painter.drawRoundedRect(vadRect, 5, 5);

        QFont vadChipFont = font();
        vadChipFont.setPointSize(9);
        vadChipFont.setWeight(QFont::DemiBold);
        painter.setFont(vadChipFont);
        painter.setPen(QColor(220, 224, 232, vadTextAlpha));
        const int trailingPadding = vadChipArrowWidth + 6;
        painter.drawText(vadRect.adjusted(vadChipHorizontalPadding, 0, -trailingPadding, 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                vadChipLabel());

        const qreal arrowX = vadRect.right() - vadChipArrowWidth;
        const qreal arrowY = vadRect.center().y() - 2.0;
        QPolygonF arrow = {
                QPointF(arrowX - 3.5, arrowY),
                QPointF(arrowX + 3.5, arrowY),
                QPointF(arrowX, arrowY + 4.5),
        };
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 224, 232, 235));
        painter.drawPolygon(arrow);

        secondaryTop += vadChipHeight + bodyLineGap;
    }

    for (const QString &secondarySubtitle : secondarySubtitles_) {
        QString secondaryBody = secondarySubtitle;
        secondaryBody = metrics.elidedText(secondaryBody, Qt::ElideRight, static_cast<int>(bodyWidth));
        painter.setPen(QColor(150, 158, 170));
        painter.drawText(QRectF(card.left() + textLeft,
                                 secondaryTop,
                                 bodyWidth,
                                 bodyTextHeight),
                secondaryBody);
        secondaryTop += bodyTextHeight + bodyLineGap;
    }

    painter.setFont(bodyFont);
    if (mode_ == Mode::Recording) {
        drawVoiceVisualizer(painter, QRectF(card.left() + textLeft,
                card.bottom() - cardBottomMargin - waveformHeightPx, bodyWidth, waveformHeightPx));
    } else {
        const QRectF bar(card.left() + textLeft,
                card.bottom() - progressBarTopFromCardBottom,
                card.width() - 100,
                5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 38));
        painter.drawRoundedRect(bar, 4, 4);
        const QRectF fill(bar.left(), bar.top(), bar.width() * audioLevel_, bar.height());
        painter.setBrush(accent);
        painter.drawRoundedRect(fill, 4, 4);
    }
}

void OverlayWidget::drawVoiceVisualizer(QPainter &painter, const QRectF &bounds) const
{
    const QString elapsed = elapsedText();
    const qreal elapsedWidthPx = QFontMetrics(painter.font()).horizontalAdvance(elapsed);
    const QRectF waveformRect(bounds.left(), bounds.top(), bounds.width() - elapsedWidthPx - 12, bounds.height());
    const qreal centerYPx = waveformRect.center().y();
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
    painter.drawLine(QPointF(waveformRect.left(), centerYPx), QPointF(waveformRect.right(), centerYPx));

    std::array<QPointF, waveformPointCount> upperPoints;
    std::array<QPointF, waveformPointCount> lowerPoints;
    for (std::size_t pointIndex = 0; pointIndex < waveformPointCount; ++pointIndex) {
        const qreal horizontalFraction = static_cast<qreal>(pointIndex) / (waveformPointCount - 1);
        const qreal xPx = waveformRect.left() + waveformRect.width() * horizontalFraction;
        const qreal phaseRadians = std::numbers::pi * horizontalFraction;
        const qreal shape = std::abs(std::sin(waveformPeakCount * phaseRadians)) * std::sin(phaseRadians);
        const qreal amplitudePx = audioLevel_ * shape * (waveformRect.height() / 2.0 - 1.0);
        upperPoints[pointIndex] = QPointF(xPx, centerYPx - amplitudePx);
        lowerPoints[pointIndex] = QPointF(xPx, centerYPx + amplitudePx);
    }

    QPainterPath waveform;
    waveform.moveTo(upperPoints.front());
    for (std::size_t sampleIndex = 1; sampleIndex < upperPoints.size(); ++sampleIndex) {
        const QPointF previous = upperPoints[sampleIndex - 1];
        const QPointF current = upperPoints[sampleIndex];
        const qreal midpointXPx = (previous.x() + current.x()) / 2.0;
        waveform.cubicTo(QPointF(midpointXPx, previous.y()), QPointF(midpointXPx, current.y()), current);
    }
    waveform.lineTo(lowerPoints.back());
    for (std::size_t sampleIndex = lowerPoints.size() - 1; sampleIndex > 0; --sampleIndex) {
        const QPointF previous = lowerPoints[sampleIndex];
        const QPointF current = lowerPoints[sampleIndex - 1];
        const qreal midpointXPx = (previous.x() + current.x()) / 2.0;
        waveform.cubicTo(QPointF(midpointXPx, previous.y()), QPointF(midpointXPx, current.y()), current);
    }
    waveform.closeSubpath();

    QLinearGradient fillGradient(waveformRect.topLeft(), waveformRect.topRight());
    fillGradient.setColorAt(0.0, QColor(255, 105, 119, 35));
    fillGradient.setColorAt(0.5, QColor(255, 105, 119, 110));
    fillGradient.setColorAt(1.0, QColor(255, 105, 119, 35));
    painter.fillPath(waveform, fillGradient);
    QLinearGradient outlineGradient(waveformRect.topLeft(), waveformRect.topRight());
    outlineGradient.setColorAt(0.0, QColor(255, 105, 119, 100));
    outlineGradient.setColorAt(0.5, QColor(255, 135, 145, 240));
    outlineGradient.setColorAt(1.0, QColor(255, 105, 119, 100));
    painter.setPen(QPen(QBrush(outlineGradient), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(waveform);

    painter.setPen(QColor(194, 200, 210));
    painter.drawText(QRectF(waveformRect.right() + 12, waveformRect.top(), elapsedWidthPx, waveformRect.height()),
            Qt::AlignVCenter | Qt::AlignLeft, elapsed);
}

void OverlayWidget::keyPressEvent(QKeyEvent *event)
{
    // Plain Escape is easy to hit by accident while dictating; require Shift+Esc.
    if (event->key() == Qt::Key_Escape && event->modifiers().testFlag(Qt::ShiftModifier)) {
        event->accept();
        emit closeRequested();
        return;
    }

    QWidget::keyPressEvent(event);
}

void OverlayWidget::mousePressEvent(QMouseEvent *event)
{
    if (miniModeEnabled_ && mode_ == Mode::Recording) {
        event->ignore();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QRectF card = rect().adjusted(8, 8, -8, -8);
    const QRectF closeRect = closeButtonRect(card);
    if (closeRect.contains(event->position())) {
        event->accept();
        emit closeRequested();
        return;
    }

    if (mode_ == Mode::Error) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QRectF sendKeywordRect = sendKeywordChipRect(card);
    if (sendKeywordRect.contains(event->position())) {
        event->accept();
        if (!sendKeywordControlEnabled_) {
            return;
        }
        emit sendKeywordToggled(!sendKeywordEnabled_);
        return;
    }

    const QRectF pasteTargetRect = pasteTargetChipRect(card);
    if (pasteTargetRect.contains(event->position())) {
        event->accept();
        if (!pasteTargetControlEnabled_) {
            return;
        }
        const QPoint menuPos = mapToGlobal(
                QPoint(static_cast<int>(pasteTargetRect.left()), static_cast<int>(pasteTargetRect.bottom() + 2)));
        showPasteTargetMenu(menuPos);
        return;
    }

    const QRectF languageRect = languageChipRect(card);
    if (languageRect.contains(event->position())) {
        event->accept();
        if (!languageControlEnabled_) {
            return;
        }

        const QPoint menuPos = mapToGlobal(
                QPoint(static_cast<int>(languageRect.left()), static_cast<int>(languageRect.bottom() + 2)));
        showLanguageMenu(menuPos);
        return;
    }

    const QRectF engineRect = engineChipRect(card);
    if (engineRect.contains(event->position())) {
        event->accept();
        if (!engineControlEnabled_) {
            return;
        }

        const QPoint menuPos = mapToGlobal(
                QPoint(static_cast<int>(engineRect.left()), static_cast<int>(engineRect.bottom() + 2)));
        showEngineMenu(menuPos);
        return;
    }

    const QRectF modelRect = modelChipRect(card);
    if (modelRect.contains(event->position())) {
        event->accept();
        if (!modelControlEnabled_) {
            return;
        }

        const QPoint menuPos = mapToGlobal(
                QPoint(static_cast<int>(modelRect.left()), static_cast<int>(modelRect.bottom() + 2)));
        showModelMenu(menuPos);
        return;
    }

    if (mode_ == Mode::Recording && vadControlAvailable_) {
        const QRectF vadRect = vadChipRect(card);
        if (vadRect.contains(event->position())) {
            event->accept();
            const QPoint menuPos = mapToGlobal(
                    QPoint(static_cast<int>(vadRect.left()), static_cast<int>(vadRect.bottom() + 2)));
            showVadMenu(menuPos);
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void OverlayWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (miniModeEnabled_ && mode_ == Mode::Recording) {
        unsetCursor();
        return;
    }
    const QRectF card = rect().adjusted(8, 8, -8, -8);
    const QRectF closeRect = closeButtonRect(card);
    const QRectF modelRect = modelChipRect(card);
    const QRectF engineRect = engineChipRect(card);
    const QRectF languageRect = languageChipRect(card);
    const QRectF pasteTargetRect = pasteTargetChipRect(card);
    const QRectF sendKeywordRect = sendKeywordChipRect(card);

    const bool overClose = closeRect.contains(event->position());
    const bool overModel = modelRect.contains(event->position()) && modelControlEnabled_
            && mode_ != Mode::Error;
    const bool overEngine = engineRect.contains(event->position()) && engineControlEnabled_
            && mode_ != Mode::Error;
    const bool overLanguage = languageRect.contains(event->position()) && languageControlEnabled_
            && mode_ != Mode::Error;
    const bool overPasteTarget = pasteTargetRect.contains(event->position()) && pasteTargetControlEnabled_
            && mode_ != Mode::Error;
    const bool overSendKeyword = sendKeywordRect.contains(event->position()) && sendKeywordControlEnabled_
            && mode_ != Mode::Error;
    const bool overVad = mode_ == Mode::Recording && vadControlAvailable_
            && vadChipRect(card).contains(event->position());

    if (overClose || overModel || overEngine || overLanguage || overPasteTarget || overSendKeyword
            || overVad) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
}

void OverlayWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
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
    emit visibilityChanged(true);
}

void OverlayWidget::updateWindowSize()
{
    if (miniModeEnabled_ && mode_ == Mode::Recording) {
        setFixedSize(miniWidthPx, miniHeightPx);
    } else if (mode_ == Mode::Error) {
        setFixedSize(errorWindowSize());
    } else {
        const int vadLine = (mode_ == Mode::Recording && vadControlAvailable_) ? 1 : 0;
        const int bodyLineCount = 1 + secondarySubtitles_.size() + vadLine;
        const int waveformExtraHeightPx = mode_ == Mode::Recording ? waveformHeightPx - 5 : 0;
        setFixedSize(defaultWidth, windowHeightForBodyLines(bodyLineCount) + waveformExtraHeightPx);
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
    const QString elapsed = QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
    return gpuMemoryLabel_.isEmpty() ? elapsed : QStringLiteral("%1 · %2").arg(elapsed, gpuMemoryLabel_);
}

QRectF OverlayWidget::modelChipRect(const QRectF &card) const
{
    QFont chipFont = font();
    chipFont.setPointSize(10);
    chipFont.setWeight(QFont::DemiBold);
    const QFontMetrics chipMetrics(chipFont);
    const int textWidth = chipMetrics.horizontalAdvance(modelLabel_);
    const int chipWidth = std::clamp(
            textWidth + modelChipHorizontalPadding * 2 + modelChipArrowWidth + 8,
            64,
            110);
    const qreal chipX = card.right() - modelChipRightPadding - closeButtonWidth - closeButtonGap - chipWidth;
    const qreal chipY = card.top() + modelChipTop;
    return QRectF(chipX, chipY, chipWidth, modelChipHeight);
}

QRectF OverlayWidget::engineChipRect(const QRectF &card) const
{
    QFont chipFont = font();
    chipFont.setPointSize(10);
    chipFont.setWeight(QFont::DemiBold);
    const QFontMetrics chipMetrics(chipFont);
    const int textWidth = chipMetrics.horizontalAdvance(engineLabel_);
    const int chipWidth = std::clamp(
            textWidth + modelChipHorizontalPadding * 2 + modelChipArrowWidth + 8,
            64,
            100);
    const QRectF modelRect = modelChipRect(card);
    const qreal chipX = modelRect.left() - chipGap - chipWidth;
    const qreal chipY = card.top() + modelChipTop;
    return QRectF(chipX, chipY, chipWidth, modelChipHeight);
}

QRectF OverlayWidget::languageChipRect(const QRectF &card) const
{
    QFont chipFont = font();
    chipFont.setPointSize(10);
    chipFont.setWeight(QFont::DemiBold);
    const QFontMetrics chipMetrics(chipFont);
    const int textWidth = chipMetrics.horizontalAdvance(languageLabel_);
    const int chipWidth = std::clamp(
            textWidth + modelChipHorizontalPadding * 2 + modelChipArrowWidth + 8,
            52,
            80);
    const QRectF engineRect = engineChipRect(card);
    const qreal chipX = engineRect.left() - chipGap - chipWidth;
    const qreal chipY = card.top() + modelChipTop;
    return QRectF(chipX, chipY, chipWidth, modelChipHeight);
}

QString OverlayWidget::sendKeywordChipLabel() const
{
    return sendKeyword_;
}

QRectF OverlayWidget::sendKeywordChipRect(const QRectF &card) const
{
    QFont chipFont = font();
    chipFont.setPointSize(10);
    chipFont.setWeight(QFont::DemiBold);
    const QFontMetrics chipMetrics(chipFont);
    const int textWidth = chipMetrics.horizontalAdvance(sendKeywordChipLabel());
    // checkbox (12) + gaps + text + padding — left of title, after indicator
    const int chipWidth = std::clamp(textWidth + 8 + 12 + 6 + 10, 52, 90);
    const qreal chipX = card.left() + sendKeywordLeft;
    const qreal chipY = card.top() + modelChipTop;
    return QRectF(chipX, chipY, chipWidth, modelChipHeight);
}

QRectF OverlayWidget::pasteTargetChipRect(const QRectF &card) const
{
    QFont chipFont = font();
    chipFont.setPointSize(10);
    chipFont.setWeight(QFont::DemiBold);
    const QFontMetrics chipMetrics(chipFont);
    const int textWidth = chipMetrics.horizontalAdvance(pasteTargetLabel_);
    const int chipWidth = std::clamp(
            textWidth + modelChipHorizontalPadding * 2 + modelChipArrowWidth + 8,
            52,
            88);
    const QRectF sendKeywordRect = sendKeywordChipRect(card);
    const qreal chipX = sendKeywordRect.right() + chipGap;
    const qreal chipY = card.top() + modelChipTop;
    return QRectF(chipX, chipY, chipWidth, modelChipHeight);
}

QRectF OverlayWidget::closeButtonRect(const QRectF &card) const
{
    const qreal x = card.right() - modelChipRightPadding - closeButtonWidth;
    const qreal y = card.top() + modelChipTop + (modelChipHeight - closeButtonWidth) / 2.0;
    return QRectF(x, y, closeButtonWidth, closeButtonWidth);
}

QRectF OverlayWidget::vadChipRect(const QRectF &card) const
{
    QFont chipFont = font();
    chipFont.setPointSize(10);
    chipFont.setWeight(QFont::DemiBold);
    const QFontMetrics chipMetrics(chipFont);
    const bool showArrow = vadControlAvailable_;
    const int textWidth = chipMetrics.horizontalAdvance(vadChipLabel());
    const int chipWidth = std::clamp(
            textWidth + vadChipHorizontalPadding * 2 + (showArrow ? vadChipArrowWidth + 8 : 0),
            vadChipMinWidth,
            std::max(vadChipMinWidth, static_cast<int>(card.width()) - textLeft - rightMargin));
    const qreal chipX = card.left() + textLeft;
    const qreal chipY = card.top() + bodyTop + bodyTextHeight + bodyLineGap;
    return QRectF(chipX, chipY, chipWidth, vadChipHeight);
}

QString OverlayWidget::vadChipLabel() const
{
    if (!vadControlAvailable_) {
        return QStringLiteral("VAD n/a");
    }
    if (!vadAutostopEnabled_) {
        return QStringLiteral("VAD off");
    }
    return QStringLiteral("VAD %1ms").arg(vadEndSilenceMs_);
}

void OverlayWidget::showModelMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *selectedAction = nullptr;
    if (availableModelPaths_.isEmpty()) {
        QAction *emptyAction = menu.addAction(QStringLiteral("No models available"));
        emptyAction->setEnabled(false);
        menu.exec(globalPos);
        return;
    }

    for (const QString &modelPath : availableModelPaths_) {
        QAction *action = menu.addAction(AppSettings::modelLabel(modelPath));
        action->setData(modelPath);
        action->setCheckable(true);
        action->setChecked(AppSettings::modelLabel(modelPath).compare(modelLabel_, Qt::CaseInsensitive) == 0);
    }

    selectedAction = menu.exec(globalPos);
    if (selectedAction != nullptr) {
        emit modelSelected(selectedAction->data().toString());
    }
}

void OverlayWidget::showEngineMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *batchAction = menu.addAction(QStringLiteral("Batch (whisper.cpp)"));
    batchAction->setData(QString::fromUtf8(AppSettings::kEngineWhisperCpp));
    batchAction->setCheckable(true);
    batchAction->setChecked(engineLabel_.compare(QStringLiteral("Batch"), Qt::CaseInsensitive) == 0);

    QAction *liveAction = menu.addAction(QStringLiteral("Live (Realtime)"));
    liveAction->setData(QString::fromUtf8(AppSettings::kEngineRealtime));
    liveAction->setCheckable(true);
    liveAction->setChecked(engineLabel_.compare(QStringLiteral("Live"), Qt::CaseInsensitive) == 0);

    QAction *buzzAsrAction = menu.addAction(QStringLiteral("BuzzASR (Polish)"));
    buzzAsrAction->setData(QString::fromUtf8(AppSettings::kEngineBuzzAsr));
    buzzAsrAction->setCheckable(true);
    buzzAsrAction->setChecked(engineLabel_.compare(QStringLiteral("BuzzASR"), Qt::CaseInsensitive) == 0);

    QAction *selectedAction = menu.exec(globalPos);
    if (selectedAction != nullptr) {
        emit engineSelected(selectedAction->data().toString());
    }
}

void OverlayWidget::showLanguageMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    const struct {
        const char *code;
        const char *title;
    } items[] = {
            {"pl", "Polish (pl)"},
            {"en", "English (en)"},
            {"auto", "Auto-detect"},
    };

    for (const auto &item : items) {
        QAction *action = menu.addAction(QString::fromUtf8(item.title));
        action->setData(QString::fromUtf8(item.code));
        action->setCheckable(true);
        action->setChecked(languageLabel_.compare(QString::fromUtf8(item.code), Qt::CaseInsensitive) == 0);
    }

    QAction *selectedAction = menu.exec(globalPos);
    if (selectedAction != nullptr) {
        emit languageSelected(selectedAction->data().toString());
    }
}

void OverlayWidget::showPasteTargetMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *pinAction = menu.addAction(QStringLiteral("Pin window (at start)"));
    pinAction->setData(QString::fromUtf8(AppSettings::kPasteTargetPin));
    pinAction->setCheckable(true);
    pinAction->setChecked(pasteTargetLabel_.compare(QStringLiteral("Pin"), Qt::CaseInsensitive) == 0);

    QAction *focusAction = menu.addAction(QStringLiteral("Follow focus"));
    focusAction->setData(QString::fromUtf8(AppSettings::kPasteTargetFocus));
    focusAction->setCheckable(true);
    focusAction->setChecked(pasteTargetLabel_.compare(QStringLiteral("Focus"), Qt::CaseInsensitive) == 0);

    QAction *selectedAction = menu.exec(globalPos);
    if (selectedAction != nullptr) {
        emit pasteTargetSelected(selectedAction->data().toString());
    }
}

void OverlayWidget::showVadMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *offAction = menu.addAction(QStringLiteral("Off"));
    offAction->setData(0);
    offAction->setCheckable(true);
    offAction->setChecked(!vadAutostopEnabled_);

    menu.addSeparator();

    for (const int presetValue : vadPresetValues) {
        QAction *action = menu.addAction(QStringLiteral("%1 ms").arg(presetValue));
        action->setData(presetValue);
        action->setCheckable(true);
        action->setChecked(vadAutostopEnabled_ && vadEndSilenceMs_ == presetValue);
    }

    QAction *selectedAction = menu.exec(globalPos);
    if (selectedAction == nullptr) {
        return;
    }

    const int presetValue = selectedAction->data().toInt();
    emit vadPresetSelected(presetValue > 0, presetValue);
}
