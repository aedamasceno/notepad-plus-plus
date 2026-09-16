#pragma once

#include <QDockWidget>
#include <QFileSystemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>

class FileBrowser : public QDockWidget
{
    Q_OBJECT

public:
    explicit FileBrowser(QWidget *parent = nullptr);
    ~FileBrowser();

    void setRootPath(const QString& path);
    
private slots:
    void onItemDoubleClicked(const QModelIndex &index);
    
private:
    QFileSystemModel *fileSystemModel;
    QTreeView *fileTreeView;
    QString currentPath;
};
