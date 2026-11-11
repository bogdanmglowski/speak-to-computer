#include "AppSettings.h"
#include "SpeakToComputerApp.h"

#include <QApplication>
#include <QLocalSocket>
#include <QStandardPaths>
#include <cstdio>

int main(int argc, char *argv[])
{
    if (argc == 2 && QByteArray(argv[1]) == "--toggle-dictation") {
        QCoreApplication app(argc, argv);
        QLocalSocket socket;
        socket.connectToServer(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                               + QStringLiteral("/speak-to-computer.sock"));
        if (!socket.waitForConnected(1000)) {
            std::fprintf(stderr, "Speak to Computer is not running or its command socket is unavailable.\n");
            return 1;
        }
        socket.write("dictate\n");
        if (!socket.waitForBytesWritten(1000)) return 1;
        return 0;
    }
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("speak-to-computer"));
    QApplication::setOrganizationName(QStringLiteral("speak-to-computer"));

    const AppSettings settings = AppSettings::load();
    SpeakToComputerApp controller(settings);
    controller.start();

    return QApplication::exec();
}
