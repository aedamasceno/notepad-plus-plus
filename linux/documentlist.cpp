#include "documentlist.h"

#include <QFileInfo>
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
}

void DocumentList::setDocuments(const QVector<DocumentListEntry> &documents, int activeIndex)
{
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
