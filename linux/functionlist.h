#pragma once

#include <QDockWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QModelIndex>

class FunctionList : public QDockWidget
{
    Q_OBJECT

public:
    explicit FunctionList(QWidget *parent = nullptr);
    ~FunctionList();

    void updateFunctions();
    void setCurrentDocument(const QString& filename);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    
private:
    QTreeWidget *functionListWidget;
    QString currentFilename;
};
