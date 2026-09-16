#include "functionlist.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QDebug>

FunctionList::FunctionList(QWidget *parent)
    : QDockWidget("Function List", parent)
{
    functionListWidget = new QTreeWidget(this);
    functionListWidget->setHeaderLabels(QStringList() << "Function" << "Line");
    functionListWidget->setRootIsDecorated(true);
    functionListWidget->setAlternatingRowColors(true);
    
    connect(functionListWidget, &QTreeWidget::itemDoubleClicked,
           this, &FunctionList::onItemDoubleClicked);
    
    setWidget(functionListWidget);
    updateFunctions(); // Initial load
}

FunctionList::~FunctionList()
{
}

void FunctionList::updateFunctions()
{
    functionListWidget->clear();
    // This is a placeholder - in real implementation this would parse 
    // the current document using the lexer to extract functions
    QTreeWidgetItem *item = new QTreeWidgetItem(functionListWidget);
    item->setText(0, "Example Function");
    item->setText(1, "15");
    functionListWidget->addTopLevelItem(item);
}

void FunctionList::setCurrentDocument(const QString& filename)
{
    currentFilename = filename;
    updateFunctions();
}

void FunctionList::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (item && !item->text(1).isEmpty())
    {
        // Would navigate editor to line number in real implementation
        qDebug() << "Navigating to function:" << item->text(0) 
                << "at line:" << item->text(1);
    }
}