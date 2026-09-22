#include "printhelper.h"
#include "Scintilla.h"
#include "ScintillaEditBase.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void configurePdfPrinter(QPrinter &printer, const QString &path)
{
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QSizeF(70.0, 70.0), QPageSize::Millimeter,
                                  QStringLiteral("Printing test page"),
                                  QPageSize::ExactMatch));
    printer.setPageMargins(QMarginsF(5.0, 5.0, 5.0, 5.0), QPageLayout::Millimeter);
    printer.setFullPage(false);
}

void setText(ScintillaEditBase &editor, const QByteArray &text)
{
    editor.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
}

int pdfPageCount(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray pdf = file.readAll();
    return pdf.count("/Type /Page") - pdf.count("/Type /Pages");
}

class RecordingEditor : public ScintillaEditBase {
public:
    explicit RecordingEditor(Sci_Position pageCapacity = 1000000,
                             bool advance = true)
        : m_pageCapacity(pageCapacity), m_advance(advance)
    {
    }

    sptr_t send(unsigned int message, uptr_t wParam = 0,
                sptr_t lParam = 0) const override
    {
        if (message != SCI_FORMATRANGEFULL)
            return ScintillaEditBase::send(message, wParam, lParam);

        const auto *range = reinterpret_cast<const Sci_RangeToFormatFull *>(lParam);
        ranges.push_back(*range);
        if (!m_advance)
            return range->chrg.cpMin;
        return std::min(range->chrg.cpMin + m_pageCapacity, range->chrg.cpMax);
    }

    mutable std::vector<Sci_RangeToFormatFull> ranges;

private:
    Sci_Position m_pageCapacity;
    bool m_advance;
};

void testEmptyDocumentIsSafe()
{
    QTemporaryDir directory;
    QPrinter printer(QPrinter::HighResolution);
    configurePdfPrinter(printer, directory.filePath(QStringLiteral("empty.pdf")));
    ScintillaEditBase editor;

    expect(PrintHelper::printScintillaDocument(&editor, printer),
           "empty Scintilla document prints without error");
}

void testRealScintillaProducesMultiplePdfPages()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("multipage.pdf"));
    QPrinter printer(QPrinter::ScreenResolution);
    configurePdfPrinter(printer, path);
    ScintillaEditBase editor;
    QByteArray content;
    for (int line = 0; line < 800; ++line)
        content += "styled printing line " + QByteArray::number(line) + "\n";
    setText(editor, content);
    editor.send(SCI_STYLESETFORE, STYLE_DEFAULT, 0x0000c0);
    editor.send(SCI_STYLECLEARALL);

    expect(PrintHelper::printScintillaDocument(&editor, printer),
           "real Scintilla document prints to PDF");
    expect(QFileInfo(path).size() > 1000, "printed PDF contains rendered output");
    expect(pdfPageCount(path) > 1, "long Scintilla document advances across PDF pages");
}

void testSelectionUsesScintillaBytePositions()
{
    QTemporaryDir directory;
    QPrinter printer(QPrinter::ScreenResolution);
    configurePdfPrinter(printer, directory.filePath(QStringLiteral("selection.pdf")));
    printer.setPrintRange(QPrinter::Selection);
    RecordingEditor editor;
    setText(editor, QStringLiteral("aé中z").toUtf8());
    editor.send(SCI_SETSEL, 1, 6);

    expect(PrintHelper::printScintillaDocument(&editor, printer),
           "selected Scintilla byte range prints");
    expect(editor.ranges.size() == 1, "selection fits on one test page");
    if (!editor.ranges.empty()) {
        expect(editor.ranges.front().chrg.cpMin == 1,
               "selection starts at the requested UTF-8 byte position");
        expect(editor.ranges.front().chrg.cpMax == 6,
               "selection ends at the requested UTF-8 byte position");
    }
}

void testAllPagesIgnoresEditorSelection()
{
    QTemporaryDir directory;
    QPrinter printer(QPrinter::ScreenResolution);
    configurePdfPrinter(printer, directory.filePath(QStringLiteral("whole.pdf")));
    printer.setPrintRange(QPrinter::AllPages);
    RecordingEditor editor;
    const QByteArray text = QStringLiteral("aé中z").toUtf8();
    setText(editor, text);
    editor.send(SCI_SETSEL, 1, 6);

    expect(PrintHelper::printScintillaDocument(&editor, printer),
           "whole document prints while an editor selection exists");
    expect(editor.ranges.size() == 1, "whole test document fits on one page");
    if (!editor.ranges.empty()) {
        expect(editor.ranges.front().chrg.cpMin == 0,
               "whole-document range starts at byte zero");
        expect(editor.ranges.front().chrg.cpMax == text.size(),
               "whole-document range ends at the Scintilla byte length");
    }
}

void testPaginationProgressAndDevicePixelRectangles()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("progress.pdf"));
    QPrinter printer(QPrinter::ScreenResolution);
    configurePdfPrinter(printer, path);
    RecordingEditor editor(4);
    setText(editor, "0123456789");

    const QRect expectedPaint = printer.pageRect(QPrinter::DevicePixel).toAlignedRect();
    const QRect expectedPage = printer.paperRect(QPrinter::DevicePixel).toAlignedRect();
    expect(PrintHelper::printScintillaDocument(&editor, printer),
           "formatter progression completes");
    expect(editor.ranges.size() == 3, "formatter is called once per page range");
    if (editor.ranges.size() == 3) {
        expect(editor.ranges[0].chrg.cpMin == 0 && editor.ranges[1].chrg.cpMin == 4 &&
                   editor.ranges[2].chrg.cpMin == 8,
               "each page resumes at the exact returned byte position");
        const Sci_Rectangle &paint = editor.ranges[0].rc;
        const Sci_Rectangle &page = editor.ranges[0].rcPage;
        expect(paint.left == expectedPaint.left() && paint.top == expectedPaint.top() &&
                   paint.right - paint.left == expectedPaint.width() &&
                   paint.bottom - paint.top == expectedPaint.height(),
               "paint rectangle uses the printer DevicePixel page rectangle");
        expect(page.left == expectedPage.left() && page.top == expectedPage.top() &&
                   page.right - page.left == expectedPage.width() &&
                   page.bottom - page.top == expectedPage.height(),
               "page rectangle uses the printer DevicePixel paper rectangle");
        expect(paint.right > paint.left && paint.bottom > paint.top &&
                   page.right > page.left && page.bottom > page.top,
               "Scintilla receives valid non-empty rectangles");
    }
    expect(pdfPageCount(path) == 3,
           "new PDF pages are created only while unformatted content remains");
}

void testNonAdvancingFormatterFailsWithoutLooping()
{
    QTemporaryDir directory;
    QPrinter printer(QPrinter::ScreenResolution);
    configurePdfPrinter(printer, directory.filePath(QStringLiteral("stalled.pdf")));
    RecordingEditor editor(4, false);
    setText(editor, "cannot advance");

    expect(!PrintHelper::printScintillaDocument(&editor, printer),
           "non-advancing Scintilla formatter reports failure");
    expect(editor.ranges.size() == 1,
           "non-advancing formatter is stopped after the first attempt");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testEmptyDocumentIsSafe();
    testRealScintillaProducesMultiplePdfPages();
    testSelectionUsesScintillaBytePositions();
    testAllPagesIgnoresEditorSelection();
    testPaginationProgressAndDevicePixelRectangles();
    testNonAdvancingFormatterFailsWithoutLooping();
    return failures == 0 ? 0 : 1;
}
