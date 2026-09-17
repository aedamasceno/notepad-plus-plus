#include "documentlist.h"
#include "documentmap.h"
#include "editorutils.h"
#include "filebrowser.h"
#include "functionlist.h"
#include "mainwindow.h"
#include "ScintillaEditBase.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <cstdio>

namespace {
int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void processFor(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

void setText(ScintillaEditBase &editor, const QByteArray &text)
{
    editor.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
}

QAction *actionWithText(QObject *root, const QString &text)
{
    for (QAction *action : root->findChildren<QAction *>()) {
        QString normalized = action->text();
        normalized.remove(QLatin1Char('&'));
        if (normalized == text && action->parent() == root)
            return action;
    }
    return nullptr;
}

QModelIndex waitForPath(QFileSystemModel *model, const QString &path)
{
    QElapsedTimer timer;
    timer.start();
    QModelIndex index;
    while (!index.isValid() && timer.elapsed() < 2000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        index = model->index(path);
    }
    return index;
}

void rejectNextDialog()
{
    QTimer::singleShot(0, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible()) {
                dialog->reject();
                return;
            }
        }
    });
}

void saveNextMessageBox()
{
    QTimer::singleShot(0, [] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                box->done(QMessageBox::Save);
                return;
            }
        }
    });
}

void testReplaceAll()
{
    ScintillaEditBase editor;
    setText(editor, "one one one");
    expect(EditorUtils::replaceAll(&editor, "one", "two", 0) == 3,
           "replaceAll replaces multiple matches");
    expect(EditorUtils::text(&editor) == "two two two", "multiple replacement content");

    setText(editor, "nothing here");
    expect(EditorUtils::replaceAll(&editor, "absent", "x", 0) == 0,
           "replaceAll reports no match");
    expect(EditorUtils::text(&editor) == "nothing here", "no-match leaves content unchanged");

    setText(editor, QStringLiteral("café café").toUtf8());
    expect(EditorUtils::replaceAll(&editor, QStringLiteral("café").toUtf8(),
                                   QStringLiteral("茶").toUtf8(), 0) == 2,
           "replaceAll uses UTF-8 byte lengths");
    expect(EditorUtils::text(&editor) == QStringLiteral("茶 茶").toUtf8(),
           "Unicode replacement content");

    setText(editor, "x x");
    expect(EditorUtils::replaceAll(&editor, "x", "xx", 0) == 2,
           "replacement containing search text terminates");
    expect(EditorUtils::text(&editor) == "xx xx", "self-containing replacement content");

    setText(editor, "cat scatter cat");
    expect(EditorUtils::replaceAll(&editor, "cat", "dog", SCFIND_WHOLEWORD) == 2,
           "replaceAll honors search options");
    expect(EditorUtils::text(&editor) == "dog scatter dog", "whole-word replacement content");
}

void testExactBackgroundDocumentWrites()
{
    ScintillaEditBase first;
    ScintillaEditBase second;
    setText(first, "first document\n");
    setText(second, QStringLiteral("second λ document\n").toUtf8());
    QTemporaryDir directory;
    const QString firstPath = directory.filePath(QStringLiteral("first.txt"));
    const QString secondPath = directory.filePath(QStringLiteral("second.txt"));
    expect(EditorUtils::writeToFile(&first, firstPath), "save first background editor");
    expect(EditorUtils::writeToFile(&second, secondPath), "save second background editor");
    QFile firstFile(firstPath);
    QFile secondFile(secondPath);
    expect(firstFile.open(QIODevice::ReadOnly), "open first saved file");
    expect(secondFile.open(QIODevice::ReadOnly), "open second saved file");
    expect(firstFile.readAll() == "first document\n", "first file has first editor bytes");
    expect(secondFile.readAll() == QStringLiteral("second λ document\n").toUtf8(),
           "second file has second editor bytes");
}

void testFunctionParsing()
{
    expect(parseFunctions("int alpha() {\n}\n", "a.cpp").size() == 1,
           "parse C++ live buffer");
    expect(parseFunctions("public void beta() {\n}\n", "A.java").size() == 1,
           "parse Java live buffer");
    expect(parseFunctions("function gamma() {\n}\nconst delta = () => 1;\n", "a.js").size() == 2,
           "parse JavaScript live buffer");
    expect(parseFunctions("def epsilon():\n    pass\n", "a.py").size() == 1,
           "parse Python live buffer");
    expect(parseFunctions("def unsaved():\n    pass\n", "").size() == 1,
           "parse unsaved Python buffer");
}

void testDocumentOverviewCoordinatesAndClick()
{
    constexpr int lineCount = 10000;
    constexpr int height = 100;
    expect(DocumentOverview::lineToY(9000, lineCount, height) == 90,
           "long document lines retain proportional vertical positions");
    expect(DocumentOverview::lineAtY(90, lineCount, height) == 9000,
           "map click uses the same proportional coordinates");
    const auto range = DocumentOverview::lineRangeForPixel(90, lineCount, height);
    expect(range.first == 9000 && range.second == 9100,
           "long documents aggregate one hundred lines per pixel row");

    DocumentOverview overview;
    overview.resize(120, height);
    QStringList lines;
    lines.reserve(lineCount);
    for (int i = 0; i < lineCount; ++i)
        lines.push_back(i % 100 == 0 ? QStringLiteral("content") : QString());
    overview.setDocument(lines.join(QLatin1Char('\n')), 9000, 50);
    int activated = -1;
    QObject::connect(&overview, &DocumentOverview::lineActivated,
                     [&activated](int line) { activated = line; });
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(10, 90), QPointF(10, 90),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&overview, &click);
    expect(activated == 9000, "document map click activates proportional document line");
}

void testWrappedAndFoldedViewportConversion()
{
    ScintillaEditBase editor;
    editor.resize(180, 120);
    editor.show();
    setText(editor, QByteArray(500, 'x') + "\nline1\nline2\nline3\nline4\n");
    editor.send(SCI_SETWRAPMODE, SC_WRAP_WORD);
    processFor(100);
    expect(editor.send(SCI_WRAPCOUNT, 0) > 1, "test fixture wraps the first document line");
    editor.send(SCI_SETFIRSTVISIBLELINE, 1);
    const auto wrapped = EditorUtils::documentViewport(&editor);
    expect(wrapped.firstLine == 0, "wrapped display line converts to its document line");
    expect(wrapped.lineCount >= 1 && wrapped.lineCount < editor.send(SCI_LINESONSCREEN),
           "wrapped viewport reports document lines rather than display lines");

    editor.send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    editor.send(SCI_HIDELINES, 1, 3);
    editor.send(SCI_SETFIRSTVISIBLELINE, 0);
    processFor(50);
    const auto folded = EditorUtils::documentViewport(&editor);
    expect(folded.firstLine == 0, "folded viewport starts at visible document line");
    expect(folded.lineCount >= 2, "folded viewport spans through the last visible document line");
    editor.hide();
}

void testDocumentListActivation()
{
    DocumentList panel;
    panel.setDocuments({{QStringLiteral("one"), {}, false},
                        {QStringLiteral("two"), {}, false}}, 0);
    auto *list = panel.findChild<QListWidget *>(QStringLiteral("DocumentListView"));
    int activated = -1;
    QObject::connect(&panel, &DocumentList::documentActivated,
                     [&activated](int index) { activated = index; });
    list->itemClicked(list->item(1));
    expect(activated == 1, "document list click activates the selected document");
    QListWidgetItem *unchanged = list->item(0);
    panel.setDocuments({{QStringLiteral("one"), {}, false},
                        {QStringLiteral("two"), {}, false}}, 1);
    expect(list->item(0) == unchanged,
           "unchanged document data updates selection without rebuilding items");
}

void testFileBrowserActivation()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("open-me.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create disposable file-browser file");
    file.write("browser content");
    file.close();

    FileBrowser browser;
    expect(browser.setRootPath(directory.path()), "set disposable file-browser root");
    auto *tree = browser.findChild<QTreeView *>(QStringLiteral("FileBrowserTree"));
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    const QModelIndex index = waitForPath(model, path);
    QString activated;
    QObject::connect(&browser, &FileBrowser::fileActivated,
                     [&activated](const QString &value) { activated = value; });
    tree->doubleClicked(index);
    expect(index.isValid() && activated == QFileInfo(path).absoluteFilePath(),
           "file-browser double click activates the file path");
}

void testMainWindowPanelOrchestrationAndUpdates()
{
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("npp-panel-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("isolated"));
    QSettings().clear();

    QMainWindow *window = createMainWindow();
    window->resize(900, 600);
    window->show();
    processFor(100);
    auto *tabs = window->findChild<QTabWidget *>();
    QAction *newAction = actionWithText(window, QStringLiteral("New"));
    QAction *documentListAction = actionWithText(window, QStringLiteral("Document List"));
    QAction *functionListAction = actionWithText(window, QStringLiteral("Function List"));
    expect(tabs && newAction && documentListAction && functionListAction,
           "main-window test seam exposes real orchestration widgets");

    newAction->trigger();
    documentListAction->trigger();
    processFor(50);
    auto *documentList = window->findChild<QListWidget *>(QStringLiteral("DocumentListView"));
    documentList->itemClicked(documentList->item(0));
    expect(tabs->currentIndex() == 0, "document-list activation switches the real tab widget");

    functionListAction->trigger();
    auto *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    auto *functionTree = window->findChild<QTreeWidget *>(QStringLiteral("FunctionListView"));
    setText(*editor, "def before():\n    pass\n");
    processFor(200);
    expect(functionTree->topLevelItemCount() == 1 &&
               functionTree->topLevelItem(0)->text(0) == QStringLiteral("before"),
           "content changes are parsed after debounce");
    setText(*editor, "def after():\n    pass\n");
    expect(functionTree->topLevelItem(0)->text(0) == QStringLiteral("before"),
           "content parsing is not rebuilt synchronously for each notification");
    processFor(200);
    expect(functionTree->topLevelItem(0)->text(0) == QStringLiteral("after"),
           "debounced content parsing catches the latest buffer");
    QTreeWidgetItem *functionItem = functionTree->topLevelItem(0);
    QListWidgetItem *documentItem = documentList->item(0);
    editor->verticalScrolled(1);
    processFor(20);
    expect(functionTree->topLevelItem(0) == functionItem,
           "scroll updates viewport without rebuilding function content");
    expect(documentList->item(0) == documentItem,
           "scroll updates do not rebuild an unchanged document list");

    functionListAction->trigger();
    setText(*editor, "def reopened():\n    pass\n");
    processFor(200);
    functionListAction->trigger();
    processFor(30);
    expect(functionTree->topLevelItemCount() == 1 &&
               functionTree->topLevelItem(0)->text(0) == QStringLiteral("reopened"),
           "hidden panel becomes current when reopened");

    delete window;
}

void activateBrowserFile(QMainWindow *window, const QString &path)
{
    auto *browser = window->findChild<FileBrowser *>(QStringLiteral("FileBrowserDock"));
    browser->setRootPath(QFileInfo(path).absolutePath());
    auto *tree = browser->findChild<QTreeView *>(QStringLiteral("FileBrowserTree"));
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    tree->doubleClicked(waitForPath(model, path));
    processFor(100);
}

void testMainWindowFileActivationBackgroundSaveAndSaveAsCancel()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString firstPath = files.filePath(QStringLiteral("first.txt"));
    const QString secondPath = files.filePath(QStringLiteral("second.txt"));
    for (const auto &entry : {qMakePair(firstPath, QByteArray("first original")),
                              qMakePair(secondPath, QByteArray("second original"))}) {
        QFile file(entry.first);
        expect(file.open(QIODevice::WriteOnly), "create disposable orchestration file");
        file.write(entry.second);
    }

    QMainWindow *window = createMainWindow();
    window->show();
    processFor(100);
    auto *tabs = window->findChild<QTabWidget *>();
    activateBrowserFile(window, firstPath);
    const int firstIndex = tabs->currentIndex();
    auto *firstEditor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    setText(*firstEditor, "saved from background");
    activateBrowserFile(window, secondPath);
    QWidget *secondTab = tabs->currentWidget();
    expect(EditorUtils::text(secondTab->findChild<ScintillaEditBase *>()) == "second original",
           "file-browser activation opens content in the real main window");

    saveNextMessageBox();
    QMetaObject::invokeMethod(window, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, firstIndex));
    QFile firstFile(firstPath);
    expect(firstFile.open(QIODevice::ReadOnly), "open background-saved file");
    expect(firstFile.readAll() == "saved from background",
           "closing a dirty background tab saves that tab's editor bytes");
    expect(tabs->currentWidget() == secondTab,
           "closing a background tab preserves the active tab");

    QAction *newAction = actionWithText(window, QStringLiteral("New"));
    QAction *saveAsAction = actionWithText(window, QStringLiteral("Save As"));
    newAction->trigger();
    QWidget *untitled = tabs->currentWidget();
    auto *untitledEditor = untitled->findChild<ScintillaEditBase *>();
    setText(*untitledEditor, "keep me");
    const int countBefore = tabs->count();
    rejectNextDialog();
    saveAsAction->trigger();
    expect(tabs->count() == countBefore && tabs->currentWidget() == untitled &&
               EditorUtils::text(untitledEditor) == "keep me" &&
               tabs->tabText(tabs->currentIndex()).endsWith(QLatin1Char('*')),
           "Save As cancellation preserves the dirty untitled tab");

    delete window;
}
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    testReplaceAll();
    testExactBackgroundDocumentWrites();
    testFunctionParsing();
    testDocumentOverviewCoordinatesAndClick();
    testWrappedAndFoldedViewportConversion();
    testDocumentListActivation();
    testFileBrowserActivation();
    testMainWindowPanelOrchestrationAndUpdates();
    testMainWindowFileActivationBackgroundSaveAndSaveAsCancel();
    if (failures == 0)
        std::puts("All Linux panel tests passed");
    return failures == 0 ? 0 : 1;
}
