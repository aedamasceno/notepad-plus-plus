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
    const int lineCount = m_lines.size();
    m_firstVisibleLine = qBound(0, firstVisibleLine, qMax(0, lineCount - 1));
    m_visibleLineCount = qBound(1, visibleLineCount, qMax(1, lineCount));
    update();
}

int DocumentOverview::lineToY(int line, int lineCount, int height)
{
    if (lineCount <= 0 || height <= 0)
        return 0;
    return qBound(0, int((qint64(qBound(0, line, lineCount)) * height) / lineCount), height);
}

int DocumentOverview::lineAtY(int y, int lineCount, int height)
{
    if (lineCount <= 0 || height <= 0)
        return 0;
    return qBound(0, int((qint64(qBound(0, y, height - 1)) * lineCount) / height),
                  lineCount - 1);
}

QPair<int, int> DocumentOverview::lineRangeForPixel(int y, int lineCount, int height)
{
    if (lineCount <= 0 || height <= 0)
        return {0, 0};
    const int boundedY = qBound(0, y, height - 1);
    const int first = int((qint64(boundedY) * lineCount) / height);
    const int lastExclusive = int((qint64(boundedY + 1) * lineCount + height - 1) / height);
    return {qBound(0, first, lineCount), qBound(first, lastExclusive, lineCount)};
}

void DocumentOverview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    const int lineCount = m_lines.size();
    if (lineCount == 0 || height() <= 0)
        return;

    painter.setPen(palette().text().color());
    for (int y = 0; y < height(); ++y) {
        const auto range = lineRangeForPixel(y, lineCount, height());
        int densityWidth = 0;
        for (int line = range.first; line < range.second; ++line) {
            const int characters = m_lines.at(line).simplified().size();
            densityWidth = qMax(densityWidth, characters * 2);
        }
        if (densityWidth > 0)
            painter.drawLine(2, y, qBound(2, densityWidth, width() - 4), y);
    }

    const int top = lineToY(m_firstVisibleLine, lineCount, height());
    const int bottom = lineToY(qMin(lineCount, m_firstVisibleLine + m_visibleLineCount),
                               lineCount, height());
    const int viewportHeight = qMax(3, bottom - top);
    QColor viewportColor = palette().highlight().color();
    viewportColor.setAlpha(55);
    const QRect viewportRect(0, qMin(top, qMax(0, height() - viewportHeight)),
                             width(), qMin(viewportHeight, height()));
    painter.fillRect(viewportRect, viewportColor);
    painter.setPen(palette().highlight().color());
    painter.drawRect(viewportRect.adjusted(0, 0, -1, -1));
}

void DocumentOverview::mousePressEvent(QMouseEvent *event)
{
    if (!m_lines.isEmpty()) {
        emit lineActivated(lineAtY(qRound(event->position().y()), m_lines.size(), height()));
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
