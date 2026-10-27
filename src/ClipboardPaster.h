#pragma once

#include <QObject>
#include <QString>

class ClipboardPaster : public QObject {
    Q_OBJECT

public:
    explicit ClipboardPaster(QObject *parent = nullptr);

    bool paste(quint64 targetWindow, const QString &text, QString *errorMessage, bool sendEnter = false);
    bool copy(const QString &text, QString *errorMessage);

    // Terminal emulators and Herdr GPUI paste with ctrl+shift+v.
    // Everything else uses ctrl+v. Herdr ignores ctrl+v: that key is passed
    // through to the pane instead of reading the clipboard.
    // A zero window means the focused window, not "always ctrl+v".
    static QString pasteKeystrokeFor(quint64 targetWindow);
    static QString pasteKeystrokeForWindowClass(const QString &windowClass);

private:
    static class QMimeData *cloneClipboardMimeData();
};
