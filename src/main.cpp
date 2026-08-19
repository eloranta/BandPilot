#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("BandPilot"));
    QApplication::setApplicationName(QStringLiteral("BandPilot"));

    MainWindow window;
    window.show();

    return QApplication::exec();
}
