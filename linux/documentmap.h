#pragma once

#include <QDockWidget>
#include <QWidget>

class DocumentOverview : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentOverview(QWidget *parent = nullptr);
    void setDocument(const QString &text, int firstVisibleLine, int visibleLineCount);
    void setViewport(int firstVisibleLine, int visibleLineCount);

signals:
    void lineActivated(int zeroBasedLine);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QStringList m_lines;
    int m_firstVisibleLine = 0;
    int m_visibleLineCount = 1;
};

class DocumentMap : public QDockWidget
{
    Q_OBJECT
public:
    explicit DocumentMap(QWidget *parent = nullptr);
    void setDocument(const QString &text, int firstVisibleLine, int visibleLineCount);
    void setViewport(int firstVisibleLine, int visibleLineCount);

signals:
    void lineActivated(int zeroBasedLine);

private:
    DocumentOverview *m_overview;
};
