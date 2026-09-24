#pragma once

#include <QDockWidget>
#include <QTreeWidget>
#include <QString>
#include <QVector>

struct FunctionEntry {
    QString name;
    int line = 0;
};

QVector<FunctionEntry> parseFunctions(const QString &text, const QString &languageId);

class FunctionList : public QDockWidget
{
    Q_OBJECT
public:
    explicit FunctionList(QWidget *parent = nullptr);
    void setDocument(const QString &text, const QString &languageId);

signals:
    void lineActivated(int zeroBasedLine);

private:
    QTreeWidget *m_tree;
    QString m_text;
    QString m_languageId;
};
