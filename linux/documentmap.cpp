#include "documentmap.h"

#include <QMouseEvent>
#include <QPainter>

DocumentOverview::DocumentOverview(QWidget *parent) : QWidget(parent)
{
    setMinimumWidth(120);
    setCursor(Qt::PointingHandCursor);
}

void DocumentOverview::setDocument(const QString &text, int firstVisibleLine, int visibleLineCount)
{
    m_lines = text.split(QLatin1Char('\n'));
    setViewport(firstVisibleLine, visibleLineCount);
}

void DocumentOverview::setViewport(int firstVisibleLine, int visibleLineCount)
{
    m_firstVisibleLine = qMax(0, firstVisibleLine);
    m_visibleLineCount = qMax(1, visibleLineCount);
    update();
}

void DocumentOverview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    if (m_lines.isEmpty())
        return;

    const qreal lineHeight = qMax<qreal>(1.0, height() / qreal(m_lines.size()));
    painter.setPen(palette().text().color());
    for (int line = 0; line < m_lines.size(); ++line) {
        const QString simplified = m_lines.at(line).simplified();
        if (simplified.isEmpty())
            continue;
        const int width = qBound(2, simplified.size() * 2, this->width() - 4);
        const int y = qMin(height() - 1, int(line * lineHeight));
        painter.drawLine(2, y, width, y);
    }

    const int top = int(m_firstVisibleLine * lineHeight);
    const int viewportHeight = qMax(3, int(m_visibleLineCount * lineHeight));
    QColor viewportColor = palette().highlight().color();
    viewportColor.setAlpha(55);
    painter.fillRect(QRect(0, top, width(), viewportHeight), viewportColor);
    painter.setPen(palette().highlight().color());
    painter.drawRect(QRect(0, top, width() - 1, viewportHeight));
}

void DocumentOverview::mousePressEvent(QMouseEvent *event)
{
    if (!m_lines.isEmpty()) {
        const int line = qBound(0, int(event->position().y() * m_lines.size() /
                                      qMax(1, height())), m_lines.size() - 1);
        emit lineActivated(line);
    }
}

DocumentMap::DocumentMap(QWidget *parent)
    : QDockWidget(tr("Document Map"), parent), m_overview(new DocumentOverview(this))
{
    setObjectName(QStringLiteral("DocumentMapDock"));
    m_overview->setObjectName(QStringLiteral("DocumentOverview"));
    setWidget(m_overview);
    connect(m_overview, &DocumentOverview::lineActivated,
            this, &DocumentMap::lineActivated);
}

void DocumentMap::setDocument(const QString &text, int firstVisibleLine, int visibleLineCount)
{
    m_overview->setDocument(text, firstVisibleLine, visibleLineCount);
}

void DocumentMap::setViewport(int firstVisibleLine, int visibleLineCount)
{
    m_overview->setViewport(firstVisibleLine, visibleLineCount);
}
