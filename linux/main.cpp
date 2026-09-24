#include "mainwindow.h"
#include "settingsmigration.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TextPinnacle"));
    QCoreApplication::setApplicationName(QStringLiteral("TextPinnacle"));
    QApplication::setApplicationDisplayName(QStringLiteral("TextPinnacle"));
    if (!migrateLegacyQSettings())
        qWarning() << "Could not migrate legacy application settings";

    QMainWindow *window = createMainWindow();
    window->show();
    return app.exec();
}
