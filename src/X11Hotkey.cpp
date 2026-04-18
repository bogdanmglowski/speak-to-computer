#include "X11Hotkey.h"

#include <QSocketNotifier>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

namespace {

int grabErrorCode = 0;

int grabErrorHandler(Display *, XErrorEvent *event)
{
    grabErrorCode = event->error_code;
    return 0;
}

bool parseHotkey(const QString &hotkey, unsigned int *modifiers, KeySym *keySymbol, QString *errorMessage)
{
    unsigned int parsedModifiers = 0;
    KeySym parsedKey = NoSymbol;
    const QStringList tokens = hotkey.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (QString token : tokens) {
        token = token.trimmed();
        const QString lower = token.toLower();
        if (lower == QStringLiteral("ctrl") || lower == QStringLiteral("control")) {
            parsedModifiers |= ControlMask;
        } else if (lower == QStringLiteral("alt") || lower == QStringLiteral("mod1")) {
            parsedModifiers |= Mod1Mask;
        } else if (lower == QStringLiteral("super") || lower == QStringLiteral("win")
                || lower == QStringLiteral("mod4")) {
            parsedModifiers |= Mod4Mask;
        } else if (lower == QStringLiteral("shift")) {
            parsedModifiers |= ShiftMask;
        } else if (lower == QStringLiteral("space")) {
            parsedKey = XK_space;
        } else {
            parsedKey = XStringToKeysym(lower.toLatin1().constData());
            if (parsedKey == NoSymbol && token.size() == 1) {
                parsedKey = XStringToKeysym(token.toUpper().toLatin1().constData());
            }
        }
    }

    if (parsedKey == NoSymbol) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not parse hotkey: %1").arg(hotkey);
        }
        return false;
    }

    *modifiers = parsedModifiers;
    *keySymbol = parsedKey;
    return true;
}

bool matchesHotkey(const XKeyEvent &event, unsigned int keycode, unsigned int modifiers)
{
    constexpr unsigned int ignoredModifiers = LockMask | Mod2Mask;
    return event.keycode == keycode && (event.state & ~ignoredModifiers) == modifiers;
}

Window readActiveWindowFromRoot(Display *display)
{
    const Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    if (activeWindowAtom == None) {
        return 0;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *propertyData = nullptr;
    const int status = XGetWindowProperty(display, DefaultRootWindow(display), activeWindowAtom, 0, 1, False,
            XA_WINDOW, &actualType, &actualFormat, &itemCount, &bytesAfter, &propertyData);
    if (status != Success || actualType != XA_WINDOW || actualFormat != 32 || itemCount != 1
            || propertyData == nullptr) {
        if (propertyData != nullptr) {
            XFree(propertyData);
        }
        return 0;
    }

    const Window activeWindow = *reinterpret_cast<Window *>(propertyData);
    XFree(propertyData);
    return activeWindow;
}

Window topLevelAncestor(Display *display, Window window)
{
    if (window == None || window == PointerRoot || window == 0) {
        return 0;
    }

    Window current = window;
    while (true) {
        Window root = 0;
        Window parent = 0;
        Window *children = nullptr;
        unsigned int childCount = 0;
        if (XQueryTree(display, current, &root, &parent, &children, &childCount) == 0) {
            if (children != nullptr) {
                XFree(children);
            }
            return current;
        }
        if (children != nullptr) {
            XFree(children);
        }
        if (parent == 0 || parent == root) {
            return current;
        }
        current = parent;
    }
}

} // namespace

X11Hotkey::X11Hotkey(QObject *parent)
    : QObject(parent)
{
}

X11Hotkey::~X11Hotkey()
{
    ungrab();
    delete notifier_;
    notifier_ = nullptr;
    if (display_ != nullptr) {
        XCloseDisplay(display_);
        display_ = nullptr;
    }
}

bool X11Hotkey::registerHotkey(const QString &hotkey, QString *errorMessage)
{
    ungrab();

    if (display_ == nullptr) {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Could not open the X11 display.");
            }
            return false;
        }
    }

    KeySym keySymbol = NoSymbol;
    if (!parseHotkey(hotkey, &modifiers_, &keySymbol, errorMessage)) {
        return false;
    }

    keycode_ = XKeysymToKeycode(display_, keySymbol);
    if (keycode_ == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not map hotkey key to an X11 keycode: %1").arg(hotkey);
        }
        return false;
    }

    rootWindow_ = DefaultRootWindow(display_);
    const unsigned int variants[] = {
            0,
            LockMask,
            Mod2Mask,
            LockMask | Mod2Mask,
    };

    grabErrorCode = 0;
    int (*previousHandler)(Display *, XErrorEvent *) = XSetErrorHandler(grabErrorHandler);
    for (unsigned int variant : variants) {
        XGrabKey(display_, static_cast<int>(keycode_), modifiers_ | variant, rootWindow_, False,
                GrabModeAsync, GrabModeAsync);
    }
    XSync(display_, False);
    XSetErrorHandler(previousHandler);

    if (grabErrorCode != 0) {
        ungrab();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not register global hotkey. It is probably already used.");
        }
        return false;
    }

    if (notifier_ == nullptr) {
        notifier_ = new QSocketNotifier(ConnectionNumber(display_), QSocketNotifier::Read, this);
        connect(notifier_, &QSocketNotifier::activated, this, &X11Hotkey::drainEvents);
    } else {
        notifier_->setEnabled(true);
    }

    return true;
}

quint64 X11Hotkey::activeWindow() const
{
    if (display_ == nullptr) {
        return 0;
    }

    const Window activeWindow = readActiveWindowFromRoot(display_);
    if (activeWindow != 0 && activeWindow != None && activeWindow != PointerRoot) {
        return static_cast<quint64>(activeWindow);
    }

    Window focused = 0;
    int revertTo = 0;
    XGetInputFocus(display_, &focused, &revertTo);
    const Window topLevelFocused = topLevelAncestor(display_, focused);
    if (topLevelFocused == 0 || topLevelFocused == None || topLevelFocused == PointerRoot) {
        return 0;
    }
    return static_cast<quint64>(topLevelFocused);
}

void X11Hotkey::drainEvents()
{
    if (display_ == nullptr) {
        return;
    }

    while (XPending(display_) > 0) {
        XEvent event;
        XNextEvent(display_, &event);
        if (event.type == KeyPress && matchesHotkey(event.xkey, keycode_, modifiers_)) {
            emit activated();
        }
    }
}

void X11Hotkey::ungrab()
{
    if (display_ == nullptr || keycode_ == 0 || rootWindow_ == 0) {
        return;
    }

    const unsigned int variants[] = {
            0,
            LockMask,
            Mod2Mask,
            LockMask | Mod2Mask,
    };
    for (unsigned int variant : variants) {
        XUngrabKey(display_, static_cast<int>(keycode_), modifiers_ | variant, rootWindow_);
    }
    XSync(display_, False);
    keycode_ = 0;
    modifiers_ = 0;
}
