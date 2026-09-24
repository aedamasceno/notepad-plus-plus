#include "documentlist.h"

#include <QFileInfo>
#include <QAction>
#include <QSignalBlocker>

DocumentList::DocumentList(QWidget *parent)
    : QDockWidget(tr("Document List"), parent), m_list(new QListWidget(this))
{
    setObjectName(QStringLiteral("DocumentListDock"));
    m_list->setObjectName(QStringLiteral("DocumentListView"));
    setWidget(m_list);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit documentActivated(item->data(Qt::UserRole).toInt());
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit documentActivated(item->data(Qt::UserRole).toInt());
    });
    m_list->setContextMenuPolicy(Qt::ActionsContextMenu);
    const auto addRequest = [this](const QString &text, const QString &name,
                                   auto signal) {
        QAction *action = new QAction(text, m_list);
        action->setObjectName(name);
        m_list->addAction(action);
        connect(action, &QAction::triggered, this, [this, signal] {
            if (QListWidgetItem *item = m_list->currentItem())
                emit (this->*signal)(item->data(Qt::UserRole).toInt());
        });
    };
    addRequest(tr("Activate"), QStringLiteral("documentListActivateAction"),
               &DocumentList::documentActivated);
    addRequest(tr("Save"), QStringLiteral("documentListSaveAction"),
               &DocumentList::saveRequested);
    addRequest(tr("Close"), QStringLiteral("documentListCloseAction"),
               &DocumentList::closeRequested);
    addRequest(tr("Close Others"), QStringLiteral("documentListCloseOthersAction"),
               &DocumentList::closeOthersRequested);
    addRequest(tr("Close to the Right"), QStringLiteral("documentListCloseRightAction"),
               &DocumentList::closeRightRequested);
}

void DocumentList::setDocuments(const QVector<DocumentListEntry> &documents, int activeIndex)
{
    if (documents == m_documents) {
        setActiveDocument(activeIndex);
        return;
    }

    m_documents = documents;
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (int index = 0; index < documents.size(); ++index) {
        const auto &document = documents.at(index);
        QString label = document.displayName;
        if (document.dirty)
            label += QLatin1Char('*');
        auto *item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(document.filePath.isEmpty() ? tr("Unsaved document") : document.filePath);
    }
    setActiveDocument(activeIndex);
}

void DocumentList::setActiveDocument(int index)
{
    if (index >= 0 && index < m_list->count())
        m_list->setCurrentRow(index);
    else
        m_list->setCurrentItem(nullptr);
}
