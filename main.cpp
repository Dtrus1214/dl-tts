#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("CrystalTts"));
    a.setApplicationDisplayName(QStringLiteral("CrystalTts"));
    a.setQuitOnLastWindowClosed(false); // tray keeps app running when window is hidden

    MainWindow w;
    w.show();
    return a.exec();
}
