#pragma once

#include <QDockWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QListWidgetItem>

class DocumentList : public QDockWidget
{
    Q_OBJECT

public:
    explicit DocumentList(QWidget *parent = nullptr);
    ~DocumentList();

    void updateDocumentList();
    void setActiveDocument(int index);
    
private slots:
    void onItemClicked(QListWidgetItem *item);
    
private:
    QListWidget *documentListWidget;
};
