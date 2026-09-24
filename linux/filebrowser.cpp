#include "filebrowser.h"

#include <QDir>
#include <QAction>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

FileBrowser::FileBrowser(QWidget *parent)
    : QDockWidget(tr("File Browser"), parent),
      m_model(new QFileSystemModel(this)), m_tree(new QTreeView(this))
{
    setObjectName(QStringLiteral("FileBrowserDock"));
    m_tree->setObjectName(QStringLiteral("FileBrowserTree"));
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_tree->setModel(m_model);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    auto *buttons = new QHBoxLayout;
    auto *chooseButton = new QPushButton(tr("Browse…"), container);
    auto *upButton = new QPushButton(tr("Up"), container);
    buttons->addWidget(chooseButton);
    buttons->addWidget(upButton);
    layout->addLayout(buttons);
    layout->addWidget(m_tree);
    layout->setContentsMargins(4, 4, 4, 4);
    setWidget(container);

    connect(chooseButton, &QPushButton::clicked, this, &FileBrowser::chooseRoot);
    connect(upButton, &QPushButton::clicked, this, [this] {
        QDir directory(m_rootPath);
        if (directory.cdUp())
            setRootPath(directory.absolutePath());
    });
    connect(m_tree, &QTreeView::doubleClicked, this, &FileBrowser::activateItem);
    m_tree->setContextMenuPolicy(Qt::ActionsContextMenu);
    QAction *refresh = new QAction(tr("Refresh"), m_tree);
    refresh->setObjectName(QStringLiteral("fileBrowserRefreshAction"));
    QAction *rename = new QAction(tr("Rename…"), m_tree);
    rename->setObjectName(QStringLiteral("fileBrowserRenameAction"));
    QAction *remove = new QAction(tr("Delete…"), m_tree);
    remove->setObjectName(QStringLiteral("fileBrowserDeleteAction"));
    m_tree->addActions({refresh, rename, remove});
    connect(refresh, &QAction::triggered, this, [this] {
        if (!m_rootPath.isEmpty()) {
            const QModelIndex root = m_model->setRootPath(m_rootPath);
            m_tree->setRootIndex(root);
        }
    });
    connect(rename, &QAction::triggered, this, [this] {
        const QString path = selectedFilePath();
        if (!path.isEmpty()) emit renameRequested(path);
    });
    connect(remove, &QAction::triggered, this, [this] {
        const QString path = selectedFilePath();
        if (!path.isEmpty()) emit deleteRequested(path);
    });
}

QString FileBrowser::selectedFilePath() const
{
    const QFileInfo info = m_model->fileInfo(m_tree->currentIndex());
    return info.isFile() ? info.absoluteFilePath() : QString();
}

bool FileBrowser::setRootPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return false;
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        return false;
    m_rootPath = canonical;
    const QModelIndex root = m_model->setRootPath(canonical);
    m_tree->setRootIndex(root);
    emit rootPathChanged(canonical);
    return true;
}

void FileBrowser::chooseRoot()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Choose File Browser Root"),
        m_rootPath.isEmpty() ? QDir::homePath() : m_rootPath);
    if (!path.isEmpty())
        setRootPath(path);
}

void FileBrowser::activateItem(const QModelIndex &index)
{
    const QFileInfo info = m_model->fileInfo(index);
    if (info.isDir())
        setRootPath(info.absoluteFilePath());
    else if (info.isFile())
        emit fileActivated(info.absoluteFilePath());
}
