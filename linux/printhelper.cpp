#include "printhelper.h"

#include "Scintilla.h"
#include "ScintillaEditBase.h"

#include <QPainter>
#include <QtPrintSupport/QPrinter>

namespace {
Sci_Rectangle scintillaRectangle(const QRectF &rectangle)
{
    const QRect pixels = rectangle.toAlignedRect();
    return {pixels.left(), pixels.top(),
            pixels.left() + pixels.width(), pixels.top() + pixels.height()};
}

bool validRectangle(const Sci_Rectangle &rectangle)
{
    return rectangle.right > rectangle.left && rectangle.bottom > rectangle.top;
}
}

bool PrintHelper::printScintillaDocument(ScintillaEditBase *editor, QPrinter &printer)
{
    if (!editor)
        return false;

    const Sci_Position documentEnd = editor->send(SCI_GETLENGTH);
    Sci_Position rangeStart = 0;
    Sci_Position rangeEnd = documentEnd;
    if (printer.printRange() == QPrinter::Selection) {
        rangeStart = editor->send(SCI_GETSELECTIONSTART);
        rangeEnd = editor->send(SCI_GETSELECTIONEND);
    }

    if (rangeStart < 0 || rangeEnd < rangeStart || rangeEnd > documentEnd)
        return false;
    if (rangeStart == rangeEnd)
        return true;

    const Sci_Rectangle pagePaintRect =
        scintillaRectangle(printer.pageRect(QPrinter::DevicePixel));
    const Sci_Rectangle pageRect =
        scintillaRectangle(printer.paperRect(QPrinter::DevicePixel));
    if (!validRectangle(pagePaintRect) || !validRectangle(pageRect))
        return false;

    QPainter painter;
    if (!painter.begin(&printer))
        return false;

    Sci_Position position = rangeStart;
    while (position < rangeEnd) {
        Sci_RangeToFormatFull formatRange{
            painter.device(),
            painter.device(),
            pagePaintRect,
            pageRect,
            {position, rangeEnd}
        };
        const Sci_Position next = editor->send(
            SCI_FORMATRANGEFULL,
            1,
            reinterpret_cast<sptr_t>(&formatRange));

        if (next <= position || next > rangeEnd)
            return false;

        position = next;
        if (position < rangeEnd && !printer.newPage())
            return false;
    }

    return true;
}
