#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("BandPilot");
    QCoreApplication::setApplicationName("BandPilot");

    MainWindow window;
    window.show();

    return app.exec();
}
