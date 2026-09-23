#pragma once

#include <QString>

class QMainWindow;
class QWidget;

QMainWindow *createMainWindow(QWidget *parent = nullptr);
bool openFileInMainWindow(QMainWindow *window, const QString &path, QString *error = nullptr);
bool saveCurrentFileInMainWindow(QMainWindow *window, QString *error = nullptr);
bool saveCurrentFileAsInMainWindow(QMainWindow *window, const QString &path,
                                   QString *error = nullptr);
