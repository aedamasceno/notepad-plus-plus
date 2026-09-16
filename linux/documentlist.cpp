#include "documentlist.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QDebug>

DocumentList::DocumentList(QWidget *parent)
    : QDockWidget("Document List", parent)
{
    documentListWidget = new QListWidget(this);
    
    connect(documentListWidget, &QListWidget::itemClicked,
            this, &DocumentList::onItemClicked);
    
    setWidget(documentListWidget);
    updateDocumentList(); // Initial load
}

DocumentList::~DocumentList()
{
}

void DocumentList::updateDocumentList()
{
    documentListWidget->clear();
    // This would be populated with actual open documents in real implementation
    QListWidgetItem *item = new QListWidgetItem("example.txt");
    item->setData(Qt::UserRole, "example.txt");
    documentListWidget->addItem(item);
    
    QListWidgetItem *item2 = new QListWidgetItem("main.cpp"); 
    item2->setData(Qt::UserRole, "main.cpp");
    documentListWidget->addItem(item2);
}

void DocumentList::setActiveDocument(int index)
{
    if (index >= 0 && index < documentListWidget->count())
    {
        documentListWidget->setCurrentRow(index);
    }
}

void DocumentList::onItemClicked(QListWidgetItem *item)
{
    QString filename = item->data(Qt::UserRole).toString();
    qDebug() << "Switching to document:" << filename;
    // Would switch to the selected document in real implementation
}