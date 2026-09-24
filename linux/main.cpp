#include "mainwindow.h"
#include "settingsmigration.h"
#include "startupfiles.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QIcon>
#include <QMainWindow>
#include <QStatusBar>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TextPinnacle"));
    QCoreApplication::setApplicationName(QStringLiteral("TextPinnacle"));
    QApplication::setApplicationDisplayName(QStringLiteral("TextPinnacle"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    if (!migrateLegacyQSettings())
        qWarning() << "Could not migrate legacy application settings";

    QMainWindow *window = createMainWindow();
    const StartupFileResult startup = openStartupFiles(window, app.arguments());
    if (!startup.failures.isEmpty()) {
        QStringList messages;
        for (const StartupFileFailure &failure : startup.failures) {
            messages.append(QStringLiteral("%1: %2").arg(failure.path, failure.error));
            qWarning().noquote() << "Could not open startup file" << failure.path << failure.error;
        }
        window->statusBar()->showMessage(
            QObject::tr("Some startup files could not be opened: %1").arg(messages.join(QStringLiteral("; "))),
            10000);
    }
    window->show();
    return app.exec();
}
