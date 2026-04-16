#pragma once

#include <QObject>
#include <QString>

class ClipboardPaster : public QObject {
    Q_OBJECT

public:
    explicit ClipboardPaster(QObject *parent = nullptr);

    bool paste(quint64 targetWindow, const QString &text, QString *errorMessage);

private:
    static class QMimeData *cloneClipboardMimeData();
};
