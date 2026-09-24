#pragma once

#include <QString>
#include <functional>

class QMainWindow;
class QWidget;
struct FileSearchRequest;

enum class ExternalSaveConflict { Modified, Deleted, ExistingSaveAsTarget };
enum class ExternalSaveDecision { Cancel, Reload, Overwrite, Recreate, SaveAs };
using ExternalSaveDecisionProvider =
    std::function<ExternalSaveDecision(ExternalSaveConflict, bool dirty)>;

QMainWindow *createMainWindow(QWidget *parent = nullptr);
bool openFileInMainWindow(QMainWindow *window, const QString &path, QString *error = nullptr);
bool saveCurrentFileInMainWindow(QMainWindow *window, QString *error = nullptr);
bool saveCurrentFileAsInMainWindow(QMainWindow *window, const QString &path,
                                   QString *error = nullptr);
void setExternalSaveDecisionProvider(QMainWindow *window,
                                     ExternalSaveDecisionProvider provider);
void startFileSearchInMainWindow(QMainWindow *window, const FileSearchRequest &request);
