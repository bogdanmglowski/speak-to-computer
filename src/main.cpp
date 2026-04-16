#include "AppSettings.h"
#include "SpeakToComputerApp.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("speak-to-computer"));
    QApplication::setOrganizationName(QStringLiteral("speak-to-computer"));

    const AppSettings settings = AppSettings::load();
    SpeakToComputerApp controller(settings);
    controller.start();

    return QApplication::exec();
}
