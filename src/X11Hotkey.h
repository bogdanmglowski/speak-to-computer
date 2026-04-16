#pragma once

#include <QObject>

class QSocketNotifier;
struct _XDisplay;

class X11Hotkey : public QObject {
    Q_OBJECT

public:
    explicit X11Hotkey(QObject *parent = nullptr);
    ~X11Hotkey() override;

    bool registerHotkey(const QString &hotkey, QString *errorMessage);
    quint64 activeWindow() const;

signals:
    void activated();

private:
    void drainEvents();
    void ungrab();

    _XDisplay *display_ = nullptr;
    QSocketNotifier *notifier_ = nullptr;
    unsigned long rootWindow_ = 0;
    unsigned int keycode_ = 0;
    unsigned int modifiers_ = 0;
};
