#pragma once

#include <QDockWidget>
#include <QListWidget>
#include <QString>
#include <QVector>

struct DocumentListEntry {
    QString displayName;
    QString filePath;
    bool dirty = false;

    bool operator==(const DocumentListEntry &other) const
    {
        return displayName == other.displayName && filePath == other.filePath &&
               dirty == other.dirty;
    }
};

class DocumentList : public QDockWidget
{
    Q_OBJECT
public:
    explicit DocumentList(QWidget *parent = nullptr);
    void setDocuments(const QVector<DocumentListEntry> &documents, int activeIndex);
    void setActiveDocument(int index);

signals:
    void documentActivated(int index);
    void saveRequested(int index);
    void closeRequested(int index);
    void closeOthersRequested(int index);
    void closeRightRequested(int index);

private:
    QListWidget *m_list;
    QVector<DocumentListEntry> m_documents;
};
