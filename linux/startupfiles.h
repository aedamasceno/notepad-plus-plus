#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class QMainWindow;

struct StartupFileFailure {
    QString path;
    QString error;
};

struct StartupFileResult {
    QStringList opened;
    QVector<StartupFileFailure> failures;
};

StartupFileResult openStartupFiles(QMainWindow *window, const QStringList &arguments,
                                   const QString &workingDirectory = QString());
