#include "X11Hotkey.h"

#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

class X11HotkeyTest : public QObject {
    Q_OBJECT

private slots:
    void mousePressAndReleaseAreSeparate();
};

void X11HotkeyTest::mousePressAndReleaseAreSeparate()
{
    X11Hotkey hotkey;
    QString errorMessage;
    QVERIFY2(hotkey.registerMouseButton(8, &errorMessage), qPrintable(errorMessage));
    QSignalSpy pressed(&hotkey, &X11Hotkey::pressed);
    QSignalSpy released(&hotkey, &X11Hotkey::released);
    QSignalSpy activated(&hotkey, &X11Hotkey::activated);

    QCOMPARE(QProcess::execute(QStringLiteral("xdotool"), {QStringLiteral("mousedown"), QStringLiteral("8")}), 0);
    QTRY_COMPARE(pressed.count(), 1);
    QCOMPARE(released.count(), 0);
    QCOMPARE(activated.count(), 0);
    QCOMPARE(QProcess::execute(QStringLiteral("xdotool"), {QStringLiteral("mouseup"), QStringLiteral("8")}), 0);
    QTRY_COMPARE(released.count(), 1);
    QCOMPARE(activated.count(), 1);
}

QTEST_GUILESS_MAIN(X11HotkeyTest)
#include "X11HotkeyTest.moc"
