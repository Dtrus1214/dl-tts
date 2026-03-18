#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QSettings>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName(QStringLiteral("CrystalTts"));
    a.setApplicationName(QStringLiteral("CrystalTts"));
    a.setApplicationDisplayName(QStringLiteral("CrystalTts"));
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    a.setQuitOnLastWindowClosed(false); // tray keeps app running when window is hidden

    // Application language (optional): load a translation file if available.
    // Looks for e.g. "<appdir>/i18n/CrystalTts_ja.qm" or resource ":/i18n/CrystalTts_ja.qm".
    auto *translator = new QTranslator(&a);
    a.setProperty("_crystaltts_translator", QVariant::fromValue(static_cast<QObject *>(translator)));
    {
        QSettings s;
        s.beginGroup(QStringLiteral("settings"));
        const QString lang = s.value(QStringLiteral("appLanguage"), QString()).toString().trimmed();
        s.endGroup();

        if (!lang.isEmpty()) {
            const QString base = QStringLiteral("CrystalTts_%1").arg(lang);
            const QString appDir = QCoreApplication::applicationDirPath();
            const QString diskPath = appDir + QStringLiteral("/i18n");
            if (translator->load(base, diskPath) || translator->load(QStringLiteral(":/i18n/") + base)) {
                a.installTranslator(translator);
            }
        }
    }

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
