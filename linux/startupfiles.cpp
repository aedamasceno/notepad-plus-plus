#include "startupfiles.h"
#include "mainwindow.h"

#include <QDir>
#include <QFileInfo>

StartupFileResult openStartupFiles(QMainWindow *window, const QStringList &arguments,
                                   const QString &workingDirectory)
{
    StartupFileResult result;
    const QDir base(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    bool positionalOnly = false;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (!positionalOnly && argument == QStringLiteral("--")) {
            positionalOnly = true;
            continue;
        }
        if (!positionalOnly && argument.startsWith(QLatin1Char('-')))
            continue;
        const QString absolute = QFileInfo(argument).isAbsolute()
            ? QDir::cleanPath(argument) : QDir::cleanPath(base.absoluteFilePath(argument));
        QString error;
        if (openFileInMainWindow(window, absolute, &error))
            result.opened.append(absolute);
        else
            result.failures.append({absolute, error});
    }
    return result;
}
