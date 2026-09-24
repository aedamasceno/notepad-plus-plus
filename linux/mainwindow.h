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
enum class CloseDecision { Save, Discard, Cancel };
using CloseDecisionProvider = std::function<CloseDecision(const QString &filePath)>;
using FileDeleteDecisionProvider = std::function<bool(const QString &filePath)>;
using DeletePostSaveHook = std::function<void(const QString &filePath)>;

QMainWindow *createMainWindow(QWidget *parent = nullptr);
bool openFileInMainWindow(QMainWindow *window, const QString &path, QString *error = nullptr);
bool saveCurrentFileInMainWindow(QMainWindow *window, QString *error = nullptr);
bool saveCurrentFileAsInMainWindow(QMainWindow *window, const QString &path,
                                   QString *error = nullptr);
bool renameFileInMainWindow(QMainWindow *window, const QString &oldPath,
                            const QString &newPath, QString *error = nullptr);
bool deleteFileInMainWindow(QMainWindow *window, const QString &path,
                            QString *error = nullptr);
void setCloseDecisionProvider(QMainWindow *window, CloseDecisionProvider provider);
void setFileDeleteDecisionProvider(QMainWindow *window,
                                   FileDeleteDecisionProvider provider);
void setExternalSaveDecisionProvider(QMainWindow *window,
                                     ExternalSaveDecisionProvider provider);
void setDeletePostSaveHook(QMainWindow *window, DeletePostSaveHook hook);
void startFileSearchInMainWindow(QMainWindow *window, const FileSearchRequest &request);
