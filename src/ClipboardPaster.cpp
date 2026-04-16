#include "ClipboardPaster.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

ClipboardPaster::ClipboardPaster(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardPaster::paste(quint64 targetWindow, const QString &text, QString *errorMessage)
{
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (xdotool.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool was not found in PATH.");
        }
        return false;
    }

    auto *clipboard = QApplication::clipboard();
    QMimeData *previousClipboard = cloneClipboardMimeData();
    clipboard->setText(text, QClipboard::Clipboard);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);

    QStringList args;
    if (targetWindow != 0) {
        args << QStringLiteral("windowactivate") << QStringLiteral("--sync") << QString::number(targetWindow);
    }
    args << QStringLiteral("key") << QStringLiteral("--clearmodifiers") << QStringLiteral("ctrl+v");

    QProcess pasteProcess;
    pasteProcess.start(xdotool, args);
    if (!pasteProcess.waitForStarted(1000)) {
        clipboard->setMimeData(previousClipboard, QClipboard::Clipboard);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start xdotool: %1").arg(pasteProcess.errorString());
        }
        return false;
    }

    if (!pasteProcess.waitForFinished(2500) || pasteProcess.exitStatus() != QProcess::NormalExit
            || pasteProcess.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(pasteProcess.readAllStandardError()).trimmed();
        clipboard->setMimeData(previousClipboard, QClipboard::Clipboard);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool paste failed: %1").arg(stderrText);
        }
        return false;
    }

    QTimer::singleShot(800, QApplication::instance(), [clipboard, previousClipboard]() {
        clipboard->setMimeData(previousClipboard, QClipboard::Clipboard);
    });
    return true;
}

QMimeData *ClipboardPaster::cloneClipboardMimeData()
{
    const QMimeData *source = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
    auto *copy = new QMimeData();
    for (const QString &format : source->formats()) {
        copy->setData(format, source->data(format));
    }
    if (source->hasText()) {
        copy->setText(source->text());
    }
    if (source->hasHtml()) {
        copy->setHtml(source->html());
    }
    if (source->hasUrls()) {
        copy->setUrls(source->urls());
    }
    if (source->hasColor()) {
        copy->setColorData(source->colorData());
    }
    if (source->hasImage()) {
        copy->setImageData(source->imageData());
    }
    return copy;
}
