#include "ClipboardPaster.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QUrl>

namespace {

bool writeX11Clipboard(const QString &text, QString *errorMessage);
quint64 activeWindowId();

QJsonObject findFocused(const QJsonObject &node)
{
    if (node.value(QStringLiteral("focused")).toBool()) return node;
    for (const QString &key : {QStringLiteral("nodes"), QStringLiteral("floating_nodes")}) {
        for (const QJsonValue &child : node.value(key).toArray()) {
            const QJsonObject found = findFocused(child.toObject());
            if (!found.isEmpty()) return found;
        }
    }
    return {};
}

QJsonObject swayFocused()
{
    QProcess process;
    process.start(QStringLiteral("swaymsg"), {QStringLiteral("-t"), QStringLiteral("get_tree"), QStringLiteral("-r")});
    if (!process.waitForFinished(1500) || process.exitCode() != 0) return {};
    return findFocused(QJsonDocument::fromJson(process.readAllStandardOutput()).object());
}

bool runWayland(const QString &program, const QStringList &args, const QByteArray &input, QString *error)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(1500)) {
        if (error) *error = QStringLiteral("Could not start %1: %2").arg(program, process.errorString());
        return false;
    }
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QStringLiteral("%1 failed: %2").arg(program, QString::fromUtf8(process.readAllStandardError()));
        return false;
    }
    return true;
}

QJsonObject focusSwayWindow(quint64 targetWindow, QString *error)
{
    if (qEnvironmentVariableIsEmpty("SWAYSOCK")) {
        if (error) *error = QStringLiteral("Wayland paste currently requires Sway.");
        return {};
    }
    if (targetWindow != 0 && !runWayland(QStringLiteral("swaymsg"),
                {QStringLiteral("[con_id=%1] focus").arg(targetWindow)}, {}, error)) return {};
    const QJsonObject focused = swayFocused();
    if ((focused.value(QStringLiteral("app_id")).isNull() && focused.value(QStringLiteral("window")).isNull())
            || (targetWindow != 0 && static_cast<quint64>(focused.value(QStringLiteral("id")).toInteger()) != targetWindow)) {
        if (error) *error = QStringLiteral("Could not focus the target Sway window; no input was sent.");
        return {};
    }
    return focused;
}

} // namespace

ClipboardPaster::ClipboardPaster(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardPaster::isWayland()
{
    const QString session = qEnvironmentVariable("XDG_SESSION_TYPE");
    return session.compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0
            || (session.isEmpty() && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"));
}

quint64 ClipboardPaster::swayFocusedWindow()
{
    const QJsonObject focused = swayFocused();
    if (focused.value(QStringLiteral("app_id")).isNull()
            && focused.value(QStringLiteral("window")).isNull()) return 0;
    return static_cast<quint64>(focused.value(QStringLiteral("id")).toInteger());
}

bool ClipboardPaster::sendWaylandKeys(quint64 targetWindow, const QStringList &keys, QString *errorMessage)
{
    if (keys.isEmpty()) return true;
    QStringList args;
    for (const QString &key : keys) {
        if (key != QStringLiteral("Return") && key != QStringLiteral("BackSpace")) {
            if (errorMessage) *errorMessage = QStringLiteral("Unsupported Wayland key: %1").arg(key);
            return false;
        }
        args << QStringLiteral("-k") << key;
    }
    if (focusSwayWindow(targetWindow, errorMessage).isEmpty()) return false;
    return runWayland(QStringLiteral("wtype"), args, {}, errorMessage);
}

bool ClipboardPaster::copy(const QString &text, QString *errorMessage)
{
    if (isWayland()) return runWayland(QStringLiteral("wl-copy"), {}, text.toUtf8(), errorMessage);
    return writeX11Clipboard(text, errorMessage);
}

bool ClipboardPaster::paste(quint64 targetWindow,
        const QString &text,
        QString *errorMessage,
        bool sendEnter,
        PasteBehavior behavior)
{
    if (isWayland()) {
        const QJsonObject focused = focusSwayWindow(targetWindow, errorMessage);
        if (focused.isEmpty()) return false;
        if (!copy(text, errorMessage)) return false;
        const QString windowClass = focused.value(QStringLiteral("app_id")).toString()
                + focused.value(QStringLiteral("window_properties")).toObject().value(QStringLiteral("class")).toString();
        const bool terminal = behavior == PasteBehavior::TerminalCompatible
                && pasteKeystrokeForWindowClass(windowClass) == QStringLiteral("ctrl+shift+v");
        QStringList args{QStringLiteral("-M"), QStringLiteral("ctrl")};
        if (terminal) args << QStringLiteral("-M") << QStringLiteral("shift");
        args << QStringLiteral("v");
        if (terminal) args << QStringLiteral("-m") << QStringLiteral("shift");
        args << QStringLiteral("-m") << QStringLiteral("ctrl");
        if (sendEnter) args << QStringLiteral("-s") << QStringLiteral("150") << QStringLiteral("-k") << QStringLiteral("Return");
        return runWayland(QStringLiteral("wtype"), args, {}, errorMessage);
    }
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (xdotool.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool was not found in PATH.");
        }
        return false;
    }

    QClipboard *clipboard = nullptr;
    QMimeData *previousClipboard = nullptr;
    const bool classic = behavior == PasteBehavior::Classic;
    if (classic) {
        clipboard = QApplication::clipboard();
        previousClipboard = cloneClipboardMimeData();
        clipboard->setText(text, QClipboard::Clipboard);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    } else {
        // Terminals read the X11 clipboard. Qt's clipboard often never becomes that selection,
        // so the paste shortcut inserts nothing. xclip owns the selection the target actually reads.
        if (!writeX11Clipboard(text, errorMessage)) {
            return false;
        }
    }

    const quint64 keystrokeWindow = classic
            ? targetWindow
            : (targetWindow != 0 ? targetWindow : activeWindowId());
    const QString keystroke = classic ? QStringLiteral("ctrl+v") : pasteKeystrokeFor(keystrokeWindow);
    qInfo().noquote() << "paste window" << keystrokeWindow << "key" << keystroke;
    QStringList args;
    if (targetWindow != 0) {
        args << QStringLiteral("windowactivate") << QStringLiteral("--sync") << QString::number(targetWindow);
    }
    args << QStringLiteral("key") << QStringLiteral("--clearmodifiers") << keystroke;
    if (sendEnter) {
        args << QStringLiteral("sleep") << QStringLiteral("0.15")
             << QStringLiteral("key") << QStringLiteral("--clearmodifiers") << QStringLiteral("Return");
    }

    QProcess pasteProcess;
    pasteProcess.start(xdotool, args);
    if (!pasteProcess.waitForStarted(1000)) {
        if (classic) {
            clipboard->setMimeData(previousClipboard);
            previousClipboard = nullptr;
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start xdotool: %1").arg(pasteProcess.errorString());
        }
        return false;
    }

    bool finished = false;
    if (classic) {
        // X11 requests the Qt-owned clipboard asynchronously. Keep processing
        // events while xdotool waits before Enter, otherwise the target can
        // receive Enter before Qt has supplied the pasted text.
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&pasteProcess, &QProcess::finished, &loop, &QEventLoop::quit);
        if (pasteProcess.state() != QProcess::NotRunning) {
            timeout.start(2500);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        }
        finished = pasteProcess.state() == QProcess::NotRunning;
        if (!finished) {
            pasteProcess.kill();
            pasteProcess.waitForFinished(1000);
        }
    } else {
        finished = pasteProcess.waitForFinished(2500);
    }
    if (!finished || pasteProcess.exitStatus() != QProcess::NormalExit
            || pasteProcess.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(pasteProcess.readAllStandardError()).trimmed();
        if (classic) {
            clipboard->setMimeData(previousClipboard);
            previousClipboard = nullptr;
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xdotool paste failed: %1").arg(stderrText);
        }
        return false;
    }
    if (classic) {
        QTimer::singleShot(800, QApplication::instance(), [clipboard, previousClipboard]() {
            clipboard->setMimeData(previousClipboard);
        });
    }
    return true;
}

QString ClipboardPaster::pasteKeystrokeFor(quint64 targetWindow)
{
    // Focus mode passes 0: the key goes to whatever is focused now, so the
    // shortcut has to come from that window, not a hardcoded ctrl+v.
    if (targetWindow == 0) {
        targetWindow = activeWindowId();
    }
    if (targetWindow == 0) {
        return QStringLiteral("ctrl+v");
    }
    QProcess xprop;
    xprop.start(QStringLiteral("xprop"),
            {QStringLiteral("-id"), QString::number(targetWindow), QStringLiteral("WM_CLASS")});
    if (!xprop.waitForFinished(500) || xprop.exitCode() != 0) {
        return QStringLiteral("ctrl+v");
    }
    return pasteKeystrokeForWindowClass(QString::fromUtf8(xprop.readAllStandardOutput()));
}

QString ClipboardPaster::pasteKeystrokeForWindowClass(const QString &windowClass)
{
    // Terminator pastes with ctrl+v, so it stays off this list.
    // Herdr GPUI (WM_CLASS so.pen.herdr-gpui) pastes with ctrl+shift+v.
    const QStringList terminalClasses = {
            QStringLiteral("kitty"),
            QStringLiteral("gnome-terminal"),
            QStringLiteral("konsole"),
            QStringLiteral("alacritty"),
            QStringLiteral("xterm"),
            QStringLiteral("wezterm"),
            QStringLiteral("tilix"),
            QStringLiteral("ghostty"),
            QStringLiteral("org.gnome.Console"),
            QStringLiteral("herdr-gpui"),
    };
    for (const QString &terminalClass : terminalClasses) {
        if (windowClass.contains(terminalClass, Qt::CaseInsensitive)) {
            return QStringLiteral("ctrl+shift+v");
        }
    }
    return QStringLiteral("ctrl+v");
}

namespace {

bool writeX11Clipboard(const QString &text, QString *errorMessage)
{
    const QString xclip = QStandardPaths::findExecutable(QStringLiteral("xclip"));
    if (xclip.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xclip was not found in PATH.");
        }
        return false;
    }
    QProcess clip;
    clip.start(xclip, {QStringLiteral("-selection"), QStringLiteral("clipboard")});
    if (!clip.waitForStarted(1000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not start xclip: %1").arg(clip.errorString());
        }
        return false;
    }
    clip.write(text.toUtf8());
    clip.closeWriteChannel();
    if (!clip.waitForFinished(1500) || clip.exitCode() != 0) {
        clip.kill();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("xclip failed: %1").arg(QString::fromUtf8(clip.readAllStandardError()));
        }
        return false;
    }
    // The forked xclip must own the selection before the paste key is sent.
    QThread::msleep(100);
    return true;
}

quint64 activeWindowId()
{
    QProcess active;
    active.start(QStringLiteral("xdotool"), {QStringLiteral("getactivewindow")});
    if (!active.waitForFinished(500) || active.exitCode() != 0) {
        return 0;
    }
    return QString::fromUtf8(active.readAllStandardOutput()).trimmed().toULongLong();
}

} // namespace

QMimeData *ClipboardPaster::cloneClipboardMimeData()
{
    const QMimeData *source = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
    auto *copy = new QMimeData();
    if (source == nullptr) {
        return copy;
    }
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
