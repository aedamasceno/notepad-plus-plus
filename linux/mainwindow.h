#pragma once

#include <QString>

class QMainWindow;
class QWidget;
struct FileSearchRequest;

QMainWindow *createMainWindow(QWidget *parent = nullptr);
bool openFileInMainWindow(QMainWindow *window, const QString &path, QString *error = nullptr);
bool saveCurrentFileInMainWindow(QMainWindow *window, QString *error = nullptr);
bool saveCurrentFileAsInMainWindow(QMainWindow *window, const QString &path,
                                   QString *error = nullptr);
void startFileSearchInMainWindow(QMainWindow *window, const FileSearchRequest &request);
