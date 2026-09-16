#include "filebrowser.h"
#include <QFileSystemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <QDir>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>

FileBrowser::FileBrowser(QWidget *parent)
    : QDockWidget("File Browser", parent)
{
    fileSystemModel = new QFileSystemModel(this);
    fileSystemModel->setRootPath(QDir::rootPath());
    
    fileTreeView = new QTreeView(this);
    fileTreeView->setModel(fileSystemModel);
    fileTreeView->setRootIndex(fileSystemModel->index(QDir::rootPath()));
    
    // Connect double-click to open files
    connect(fileTreeView, &QTreeView::doubleClicked,
            this, &FileBrowser::onItemDoubleClicked);
    
    QWidget *widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(widget);
    layout->addWidget(fileTreeView);
    setWidget(widget);
}

FileBrowser::~FileBrowser()
{
}

void FileBrowser::setRootPath(const QString& path)
{
    currentPath = path;
    fileSystemModel->setRootPath(path);
    fileTreeView->setRootIndex(fileSystemModel->index(path));
}

void FileBrowser::onItemDoubleClicked(const QModelIndex &index)
{
    QFileInfo fileInfo = fileSystemModel->fileInfo(index);
    
    if (fileInfo.isFile())
    {
        // Would open file in editor in real implementation
        qDebug() << "Opening file:" << fileInfo.absoluteFilePath();
    }
    else
    {
        // Directory navigation - update view
        setRootPath(fileInfo.absoluteFilePath());
    }
}