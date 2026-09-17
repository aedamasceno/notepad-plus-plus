#pragma once

#include <QDockWidget>
#include <QFileSystemModel>
#include <QTreeView>

class FileBrowser : public QDockWidget
{
    Q_OBJECT
public:
    explicit FileBrowser(QWidget *parent = nullptr);
    bool setRootPath(const QString &path);
    QString rootPath() const { return m_rootPath; }

signals:
    void fileActivated(const QString &filePath);
    void rootPathChanged(const QString &rootPath);

private:
    void chooseRoot();
    void activateItem(const QModelIndex &index);
    QFileSystemModel *m_model;
    QTreeView *m_tree;
    QString m_rootPath;
};
