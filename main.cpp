#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("CrystalTts"));
    a.setApplicationDisplayName(QStringLiteral("CrystalTts"));
    a.setQuitOnLastWindowClosed(false); // tray keeps app running when window is hidden

    const QString lockDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!lockDir.isEmpty())
        QDir().mkpath(lockDir);
    const QString lockPath = (lockDir.isEmpty() ? QDir::tempPath() : lockDir) + QLatin1String("/CrystalTts.lock");
    QLockFile instanceLock(lockPath);
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0)) {
        QMessageBox::information(nullptr,
                                 QObject::tr("CrystalTts"),
                                 QObject::tr("CrystalTts is already running."));
        return 0;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
