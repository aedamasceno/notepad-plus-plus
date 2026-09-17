#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Notepad++"));
    QCoreApplication::setApplicationName(QStringLiteral("Notepad++ Linux"));

    QMainWindow *window = createMainWindow();
    window->show();
    return app.exec();
}
