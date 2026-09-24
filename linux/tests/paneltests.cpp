#include "documentlist.h"
#include "documentmap.h"
#include "editorutils.h"
#include "filebrowser.h"
#include "functionlist.h"
#include "mainwindow.h"
#include "sessionmanager.h"
#include "ScintillaEditBase.h"

#include <QAction>

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QSplitter>
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

void submitRenameDialog(const QString &name)
{
    QTimer::singleShot(0, [name] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QInputDialog *>(widget);
            if (!dialog || !dialog->isVisible())
                continue;
            if (auto *lineEdit = dialog->findChild<QLineEdit *>())
                lineEdit->setText(name);
            QTimer::singleShot(0, [] {
                for (QWidget *candidate : QApplication::topLevelWidgets()) {
                    if (auto *box = qobject_cast<QMessageBox *>(candidate);
                        box && box->isVisible()) {
                        box->accept();
                        return;
                    }
                }
            });
            dialog->accept();
            return;
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
    expect(parseFunctions("int alpha() {\n}\n", "cpp").size() == 1,
           "parse C++ live buffer");
    expect(parseFunctions("public void beta() {\n}\n", "java").size() == 1,
           "parse Java live buffer");
    expect(parseFunctions("function gamma() {\n}\nconst delta = () => 1;\n", "javascript").size() == 2,
           "parse JavaScript live buffer");
    expect(parseFunctions("def epsilon():\n    pass\n", "python").size() == 1,
           "parse Python live buffer");
    expect(parseFunctions("def stale():\n    pass\n", "plain").isEmpty(),
           "unsupported logical language clears rather than guessing from content");
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

    int saved = -1, closed = -1, closedOthers = -1, closedRight = -1;
    QObject::connect(&panel, &DocumentList::saveRequested, [&](int i) { saved = i; });
    QObject::connect(&panel, &DocumentList::closeRequested, [&](int i) { closed = i; });
    QObject::connect(&panel, &DocumentList::closeOthersRequested, [&](int i) { closedOthers = i; });
    QObject::connect(&panel, &DocumentList::closeRightRequested, [&](int i) { closedRight = i; });
    list->setCurrentRow(1);
    QAction *activateAction = panel.findChild<QAction *>("documentListActivateAction");
    expect(activateAction, "document list exposes an Activate context action");
    if (activateAction) activateAction->trigger();
    panel.findChild<QAction *>("documentListSaveAction")->trigger();
    panel.findChild<QAction *>("documentListCloseAction")->trigger();
    panel.findChild<QAction *>("documentListCloseOthersAction")->trigger();
    panel.findChild<QAction *>("documentListCloseRightAction")->trigger();
    expect(saved == 1 && closed == 1 && closedOthers == 1 && closedRight == 1,
           "document-list context actions preserve the selected logical ordering index");
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
    QString renameRequested, deleteRequested;
    QObject::connect(&browser, &FileBrowser::renameRequested,
                     [&](const QString &value) { renameRequested = value; });
    QObject::connect(&browser, &FileBrowser::deleteRequested,
                     [&](const QString &value) { deleteRequested = value; });
    tree->setCurrentIndex(index);
    browser.findChild<QAction *>("fileBrowserRenameAction")->trigger();
    browser.findChild<QAction *>("fileBrowserDeleteAction")->trigger();
    expect(renameRequested == QFileInfo(path).absoluteFilePath() &&
               deleteRequested == QFileInfo(path).absoluteFilePath(),
           "file-browser destructive actions emit file-only semantic requests");
    browser.findChild<QAction *>("fileBrowserRefreshAction")->trigger();
    expect(waitForPath(model, path).isValid(), "file-browser refresh preserves visible files");
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
    window->findChild<QAction *>(QStringLiteral("languagePythonAction"))->trigger();
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

    setCloseDecisionProvider(window, [](const QString &) { return CloseDecision::Save; });
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

void testOpenFileRenameUpdatesLogicalPathAndLanguage()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString cpp = files.filePath(QStringLiteral("rename.cpp"));
    const QString python = files.filePath(QStringLiteral("rename.py"));
    const QString collision = files.filePath(QStringLiteral("collision.py"));
    QFile source(cpp); expect(source.open(QIODevice::WriteOnly), "create rename source");
    source.write("int renamed() {}\n"); source.close();
    QFile occupied(collision); expect(occupied.open(QIODevice::WriteOnly), "create rename collision");
    occupied.write("occupied"); occupied.close();
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), cpp, &error), "open rename source");
    expect(renameFileInMainWindow(window.get(), cpp, python, &error) &&
               !QFileInfo::exists(cpp) && QFileInfo::exists(python) &&
               window->findChild<QAction *>("languagePythonAction")->isChecked(),
           "renaming an open automatic document updates path and redetects language");
    expect(!renameFileInMainWindow(window.get(), python, collision, &error) &&
               QFileInfo::exists(python),
           "rename refuses overwrite collisions without moving the source");
    window->findChild<QAction *>("languageJavascriptAction")->trigger();
    const QString text = files.filePath(QStringLiteral("rename.txt"));
    expect(renameFileInMainWindow(window.get(), python, text, &error) &&
               window->findChild<QAction *>("languageJavascriptAction")->isChecked(),
           "renaming an explicit-language document preserves its override");
    const auto documents = window->findChild<SessionManager *>()->documents();
    expect(!documents.isEmpty() && documents.last().filePath == text,
           "open rename updates recovery/session logical path");

    QDir().mkpath(files.filePath(QStringLiteral("nested")));
    const QString crossDirectory = files.filePath(QStringLiteral("nested/moved.txt"));
    error.clear();
    const bool crossDirectoryResult = renameFileInMainWindow(
        window.get(), text, crossDirectory, &error);
    expect(!crossDirectoryResult && QFileInfo::exists(text) &&
               !QFileInfo::exists(crossDirectory) &&
               error.contains(QStringLiteral("same folder"), Qt::CaseInsensitive),
           "file-browser rename seam rejects cross-directory destinations");
    if (crossDirectoryResult)
        QFile::rename(crossDirectory, text);

    auto *browser = window->findChild<FileBrowser *>(QStringLiteral("FileBrowserDock"));
    expect(browser->setRootPath(files.path()), "set rename fixture as browser root");
    auto *tree = browser->findChild<QTreeView *>(QStringLiteral("FileBrowserTree"));
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    QAction *renameAction = browser->findChild<QAction *>(QStringLiteral("fileBrowserRenameAction"));
    for (const QString &invalidName : {QStringLiteral("."), QStringLiteral(".."),
             QStringLiteral("nested/name.txt"), QStringLiteral("unsafe\\name.txt"),
             QStringLiteral("nested/../component.txt")}) {
        tree->setCurrentIndex(waitForPath(model, text));
        submitRenameDialog(invalidName);
        renameAction->trigger();
        const QString attemptedPath = QFileInfo(text).dir().filePath(invalidName);
        const bool sourcePreserved = QFileInfo::exists(text);
        expect(sourcePreserved,
               "file-browser rename dialog rejects non-basename input without moving the source");
        if (!sourcePreserved && QFileInfo(attemptedPath).isFile())
            QFile::rename(attemptedPath, text);
    }

    const QString backslashDestination = files.filePath(QStringLiteral("direct\\unsafe.txt"));
    error.clear();
    const bool backslashResult = renameFileInMainWindow(
        window.get(), text, backslashDestination, &error);
    expect(!backslashResult && QFileInfo::exists(text) &&
               !QFileInfo::exists(backslashDestination),
           "file-browser rename seam rejects backslashes in the destination basename");
    if (backslashResult)
        QFile::rename(backslashDestination, text);
}

void testDocumentListRoutesMainWindowDocumentCommands()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    QStringList paths;
    for (int i = 0; i < 4; ++i) {
        const QString path = files.filePath(QStringLiteral("document-%1.txt").arg(i));
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "create document-list routing fixture");
        file.write(QByteArray("disk-") + QByteArray::number(i));
        file.close();
        paths << path;
    }
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show();
    processFor(30);
    QString error;
    for (const QString &path : paths)
        expect(openFileInMainWindow(window.get(), path, &error), "open routing fixture");
    actionWithText(window.get(), QStringLiteral("Document List"))->trigger();
    auto *list = window->findChild<QListWidget *>(QStringLiteral("DocumentListView"));
    auto *primary = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    expect(list->count() == 4, "document list contains each logical document once");

    primary->setCurrentIndex(0);
    actionWithText(window.get(), QStringLiteral("Clone to Other View"))->trigger();
    processFor(30);
    expect(list->count() == 4, "document list deduplicates cloned views");
    list->setCurrentRow(2);
    window->findChild<QAction *>("documentListActivateAction")->trigger();
    expect(primary->tabText(primary->currentIndex()) == QStringLiteral("document-2.txt"),
           "document-list Activate routes through MainWindow");

    auto *editor = primary->currentWidget()->findChild<ScintillaEditBase *>();
    setText(*editor, "saved-by-document-list");
    window->findChild<QAction *>("documentListSaveAction")->trigger();
    QFile saved(paths.at(2));
    expect(saved.open(QIODevice::ReadOnly) && saved.readAll() == "saved-by-document-list",
           "document-list Save writes the selected logical document");

    list->setCurrentRow(1);
    window->findChild<QAction *>("documentListCloseRightAction")->trigger();
    processFor(20);
    expect(list->count() == 2, "document-list Close Right closes later logical documents");
    window->findChild<QAction *>("documentListCloseOthersAction")->trigger();
    processFor(20);
    expect(list->count() == 1, "document-list Close Others preserves only the selected document");

    expect(openFileInMainWindow(window.get(), paths.at(3), &error), "reopen dirty close fixture");
    processFor(20);
    list->setCurrentRow(1);
    auto *dirtyEditor = primary->currentWidget()->findChild<ScintillaEditBase *>();
    setText(*dirtyEditor, "cancelled-close");
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Cancel; });
    window->findChild<QAction *>("documentListCloseAction")->trigger();
    expect(list->count() == 2 && QFileInfo::exists(paths.at(3)),
           "document-list Close honors dirty Cancel");
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Save; });
    window->findChild<QAction *>("documentListCloseAction")->trigger();
    QFile savedOnClose(paths.at(3));
    expect(savedOnClose.open(QIODevice::ReadOnly) && savedOnClose.readAll() == "cancelled-close" &&
               list->count() == 1,
           "document-list Close honors dirty Save");

    expect(openFileInMainWindow(window.get(), paths.at(0), &error), "open discard close fixture");
    processFor(20);
    list->setCurrentRow(1);
    setText(*primary->currentWidget()->findChild<ScintillaEditBase *>(), "discarded-close");
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Discard; });
    window->findChild<QAction *>("documentListCloseAction")->trigger();
    QFile discarded(paths.at(0));
    expect(discarded.open(QIODevice::ReadOnly) && discarded.readAll() == "disk-0" &&
               list->count() == 1,
           "document-list Close honors dirty Discard without saving");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDocumentListBulkCloseCancelIsAtomic()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    QStringList paths;
    for (int i = 0; i < 3; ++i) {
        const QString path = files.filePath(QStringLiteral("bulk-%1.txt").arg(i));
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "create bulk-close fixture");
        file.write(QByteArray("disk-") + QByteArray::number(i));
        file.close();
        paths << path;
    }
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show(); processFor(30);
    QString error;
    for (const QString &path : paths)
        expect(openFileInMainWindow(window.get(), path, &error), "open bulk-close fixture");
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    for (int i = 1; i < 3; ++i)
        setText(*tabs->widget(i)->findChild<ScintillaEditBase *>(),
                QByteArray("unsaved-") + QByteArray::number(i));
    actionWithText(window.get(), QStringLiteral("Document List"))->trigger();
    auto *list = window->findChild<QListWidget *>(QStringLiteral("DocumentListView"));
    setCloseDecisionProvider(window.get(), [&](const QString &path) {
        return path == paths.at(1) ? CloseDecision::Save : CloseDecision::Cancel;
    });
    list->setCurrentRow(0);
    window->findChild<QAction *>("documentListCloseRightAction")->trigger();
    expect(list->count() == 3 && tabs->count() == 3,
           "bulk Close Right cancellation removes no logical documents");
    expect(EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) == "unsaved-1" &&
               EditorUtils::text(tabs->widget(2)->findChild<ScintillaEditBase *>()) == "unsaved-2" &&
               tabs->tabText(1).endsWith('*') && tabs->tabText(2).endsWith('*'),
           "bulk close cancellation preserves every staged decision buffer exactly");
    QFile firstDisk(paths.at(1));
    expect(firstDisk.open(QIODevice::ReadOnly) && firstDisk.readAll() == "disk-1",
           "later bulk Cancel prevents an earlier staged Save from writing disk");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDocumentListClonedDirtyCloseCancelPreservesBothViews()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("cloned-cancel.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create cloned-close Cancel fixture");
    file.write("disk content");
    file.close();

    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->resize(900, 500);
    window->show();
    processFor(30);
    QString error;
    expect(openFileInMainWindow(window.get(), path, &error), "open cloned-close Cancel fixture");
    auto *primary = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    auto *secondary = window->findChild<QTabWidget *>(QStringLiteral("secondaryTabWidget"));
    auto *splitter = window->findChild<QSplitter *>(QStringLiteral("editorViewSplitter"));
    actionWithText(window.get(), QStringLiteral("Clone to Other View"))->trigger();
    processFor(30);
    splitter->setSizes({310, 590});
    processFor(20);

    QWidget *primaryView = primary->widget(0);
    QWidget *secondaryView = secondary->widget(0);
    auto *primaryEditor = primaryView->findChild<ScintillaEditBase *>();
    auto *secondaryEditor = secondaryView->findChild<ScintillaEditBase *>();
    const QByteArray dirtyContent = QByteArray(80, 'x') + "\nunsaved cloned content\n";
    setText(*primaryEditor, dirtyContent);
    primaryEditor->send(SCI_SETSEL, 2, 9);
    secondaryEditor->send(SCI_SETSEL, 12, 25);

    actionWithText(window.get(), QStringLiteral("Document List"))->trigger();
    processFor(20);
    const QList<int> originalSizes = splitter->sizes();
    auto *list = window->findChild<QListWidget *>(QStringLiteral("DocumentListView"));
    list->setCurrentRow(0);
    int decisions = 0;
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        ++decisions;
        return CloseDecision::Cancel;
    });
    window->findChild<QAction *>("documentListCloseAction")->trigger();
    processFor(20);

    expect(decisions == 1, "cloned dirty Document List Close prompts exactly once");
    expect(primary->count() == 1 && secondary->count() == 1 &&
               primary->widget(0) == primaryView && secondary->widget(0) == secondaryView,
           "cloned dirty Close Cancel preserves both views in their original panes");
    expect(splitter->sizes() == originalSizes &&
               primaryEditor->send(SCI_GETANCHOR) == 2 &&
               primaryEditor->send(SCI_GETCURRENTPOS) == 9 &&
               secondaryEditor->send(SCI_GETANCHOR) == 12 &&
               secondaryEditor->send(SCI_GETCURRENTPOS) == 25,
           "cloned dirty Close Cancel preserves layout and independent view state");
    expect(EditorUtils::text(primaryEditor) == dirtyContent &&
               EditorUtils::text(secondaryEditor) == dirtyContent &&
               primary->tabText(0).endsWith('*') && secondary->tabText(0).endsWith('*'),
           "cloned dirty Close Cancel preserves exact unsaved content and dirty state");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDocumentListClonedDirtyCloseSaveAndDiscardCloseAllViews()
{
    for (CloseDecision decision : {CloseDecision::Save, CloseDecision::Discard}) {
        QTemporaryDir settingsDirectory;
        QTemporaryDir files;
        QTemporaryDir sessionDirectory;
        const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
        qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
        QSettings().clear();
        const QString path = files.filePath(decision == CloseDecision::Save
            ? QStringLiteral("cloned-save.txt") : QStringLiteral("cloned-discard.txt"));
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "create cloned-close completion fixture");
        file.write("disk content");
        file.close();

        std::unique_ptr<QMainWindow> window(createMainWindow());
        window->show();
        processFor(20);
        QString error;
        expect(openFileInMainWindow(window.get(), path, &error),
               "open cloned-close completion fixture");
        auto *primary = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
        auto *secondary = window->findChild<QTabWidget *>(QStringLiteral("secondaryTabWidget"));
        actionWithText(window.get(), QStringLiteral("Clone to Other View"))->trigger();
        processFor(20);
        const QByteArray dirtyContent = decision == CloseDecision::Save
            ? QByteArray("saved cloned content") : QByteArray("discarded cloned content");
        setText(*primary->widget(0)->findChild<ScintillaEditBase *>(), dirtyContent);
        actionWithText(window.get(), QStringLiteral("Document List"))->trigger();
        processFor(20);
        auto *list = window->findChild<QListWidget *>(QStringLiteral("DocumentListView"));
        list->setCurrentRow(0);
        int decisions = 0;
        setCloseDecisionProvider(window.get(), [&](const QString &) {
            ++decisions;
            return decision;
        });
        window->findChild<QAction *>("documentListCloseAction")->trigger();
        processFor(20);

        expect(decisions == 1, "cloned dirty completed Close prompts exactly once");
        expect(primary->count() + secondary->count() == 1 && secondary->count() == 0,
               "cloned dirty Save or Discard closes every view before creating one placeholder");
        QFile result(path);
        expect(result.open(QIODevice::ReadOnly), "read cloned-close completion result");
        expect(result.readAll() == (decision == CloseDecision::Save
                    ? dirtyContent : QByteArray("disk content")),
               "cloned dirty Save persists once while Discard leaves disk unchanged");
        window.reset();
        qputenv("NPP_SESSION_DIR", previousSession);
    }
}

void testFileBrowserDeleteConfirmationAndDirtySafety()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("delete-me.txt"));
    QFile file(path); expect(file.open(QIODevice::WriteOnly), "create delete fixture");
    file.write("original"); file.close();
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), path, &error), "open delete fixture");
    auto *tabs = window->findChild<QTabWidget *>("primaryTabWidget");
    setText(*tabs->currentWidget()->findChild<ScintillaEditBase *>(), "dirty");
    auto *browser = window->findChild<FileBrowser *>("FileBrowserDock");
    expect(browser->setRootPath(files.path()), "set delete fixture as browser root");
    auto *tree = browser->findChild<QTreeView *>(QStringLiteral("FileBrowserTree"));
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    tree->setCurrentIndex(waitForPath(model, path));
    QAction *deleteAction = browser->findChild<QAction *>("fileBrowserDeleteAction");
    const int initialCount = tabs->count();

    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return false; });
    deleteAction->trigger();
    expect(QFileInfo::exists(path) && tabs->count() == initialCount,
           "delete confirmation defaults to cancellation without closing the editor");

    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Cancel; });
    deleteAction->trigger();
    expect(QFileInfo::exists(path) && tabs->tabText(tabs->currentIndex()).endsWith('*'),
           "dirty open delete respects the existing close Cancel decision");

    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Discard; });
    deleteAction->trigger();
    expect(!QFileInfo::exists(path),
           "confirmed Discard closes the logical document before deleting the file");
}

void testDeleteHandlesDirtySaveAndConfirmationReplacement()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString savePath = files.filePath(QStringLiteral("save-before-delete.txt"));
    QFile saveFile(savePath); expect(saveFile.open(QIODevice::WriteOnly), "create save-delete fixture");
    saveFile.write("before"); saveFile.close();
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), savePath, &error), "open save-delete fixture");
    auto *tabs = window->findChild<QTabWidget *>("primaryTabWidget");
    setText(*tabs->currentWidget()->findChild<ScintillaEditBase *>(), "saved then deleted");
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Save; });
    expect(deleteFileInMainWindow(window.get(), savePath, &error) && !QFileInfo::exists(savePath),
           "dirty Save updates the expected identity and then deletes the saved file");

    const QString replacePath = files.filePath(QStringLiteral("replace-during-confirm.txt"));
    const QString movedPath = files.filePath(QStringLiteral("selected-before-confirm.txt"));
    QFile selected(replacePath); expect(selected.open(QIODevice::WriteOnly), "create confirm-race fixture");
    selected.write("selected"); selected.close();
    expect(openFileInMainWindow(window.get(), replacePath, &error), "open confirm-race fixture");
    setText(*tabs->currentWidget()->findChild<ScintillaEditBase *>(), "keep dirty editor");
    int closeDecisions = 0;
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        ++closeDecisions;
        return CloseDecision::Discard;
    });
    setFileDeleteDecisionProvider(window.get(), [&](const QString &) {
        expect(QFile::rename(replacePath, movedPath), "replace pathname during confirmation");
        QFile replacement(replacePath);
        expect(replacement.open(QIODevice::WriteOnly), "create confirmation replacement");
        replacement.write("confirmation replacement");
        replacement.close();
        return true;
    });
    const int tabCount = tabs->count();
    expect(!deleteFileInMainWindow(window.get(), replacePath, &error) &&
               error.contains(QStringLiteral("changed"), Qt::CaseInsensitive),
           "delete revalidates identity immediately after confirmation");
    expect(closeDecisions == 0 && tabs->count() == tabCount &&
               tabs->tabText(tabs->currentIndex()).endsWith('*'),
           "confirmation-time replacement aborts before closing the dirty editor");
    QFile replacement(replacePath);
    expect(replacement.open(QIODevice::ReadOnly) && replacement.readAll() == "confirmation replacement",
           "confirmation-time replacement remains untouched");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteRejectsPathReplacementAfterDirtyDecision()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("delete-race.txt"));
    const QString original = files.filePath(QStringLiteral("original-moved.txt"));
    QFile file(path); expect(file.open(QIODevice::WriteOnly), "create delete race fixture");
    file.write("selected-original"); file.close();
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), path, &error), "open delete race fixture");
    auto *tabs = window->findChild<QTabWidget *>("primaryTabWidget");
    setText(*tabs->currentWidget()->findChild<ScintillaEditBase *>(), "dirty editor");
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        expect(QFile::rename(path, original), "replace selected pathname during dirty decision");
        QFile replacement(path);
        expect(replacement.open(QIODevice::WriteOnly), "create pathname replacement");
        replacement.write("replacement-must-survive");
        replacement.close();
        return CloseDecision::Discard;
    });
    expect(!deleteFileInMainWindow(window.get(), path, &error) &&
               error.contains(QStringLiteral("changed"), Qt::CaseInsensitive),
           "delete aborts when pathname identity changes during modal decisions");
    QFile replacement(path);
    expect(replacement.open(QIODevice::ReadOnly) &&
               replacement.readAll() == "replacement-must-survive" && QFileInfo::exists(original),
           "delete never removes the replacement pathname");
    expect(tabs->count() == 1 && tabs->tabText(0).endsWith('*') &&
               EditorUtils::text(tabs->widget(0)->findChild<ScintillaEditBase *>()) == "dirty editor",
           "identity-change abort preserves the dirty editor and exact unsaved buffer");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteRejectsPathReplacementImmediatelyAfterSave()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("post-save-race.txt"));
    const QString savedObject = files.filePath(QStringLiteral("intentional-save.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create post-save replacement fixture");
    file.write("original bytes");
    file.close();

    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), path, &error),
           "open post-save replacement fixture");
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    const QByteArray intended("editor bytes intentionally saved");
    setText(*tabs->currentWidget()->findChild<ScintillaEditBase *>(), intended);
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Save; });
    setDeletePostSaveHook(window.get(), [&](const QString &savedPath) {
        expect(savedPath == path, "post-save hook receives the deletion pathname");
        expect(QFile::rename(path, savedObject),
               "replace pathname immediately after successful Save");
        QFile replacement(path);
        expect(replacement.open(QIODevice::WriteOnly), "create unrelated post-save replacement");
        replacement.write("unrelated replacement must survive");
        replacement.close();
    });

    expect(!deleteFileInMainWindow(window.get(), path, &error) &&
               error.contains(QStringLiteral("changed"), Qt::CaseInsensitive),
           "delete aborts instead of adopting the post-save replacement");
    QFile replacement(path);
    QFile saved(savedObject);
    expect(replacement.open(QIODevice::ReadOnly) &&
               replacement.readAll() == "unrelated replacement must survive",
           "unrelated post-save replacement remains at the selected pathname");
    expect(saved.open(QIODevice::ReadOnly) && saved.readAll() == intended,
           "the file object produced by Save is not confused with its replacement");
    auto *editor = tabs->widget(0)->findChild<ScintillaEditBase *>();
    auto *session = window->findChild<SessionManager *>();
    const auto documents = session->documents();
    expect(tabs->count() == 1 && tabs->tabText(0).endsWith('*') &&
               EditorUtils::text(editor) == intended,
           "aborted delete preserves the editor buffer as dirty and recoverable");
    expect(documents.size() == 1 && documents.first().dirty &&
               session->readRecoveryContent(documents.first()).content == intended,
           "aborted delete keeps the session checkpoint consistent with the editor");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteCleanAndDirtyLogicalDocumentsAtSamePathCancelsAtomically()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("duplicate-path.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create duplicate-path delete fixture");
    file.write("disk content");
    file.close();
    {
        SessionManager writer(nullptr, sessionDirectory.path(), 20);
        writer.loadSession();
        writer.updateDocument({QStringLiteral("clean-id"), path, 0, false}, {});
        writer.updateDocument({QStringLiteral("dirty-id"), path, 0, true},
                              QByteArrayLiteral("unsaved duplicate content"));
        writer.setSessionLayout({QStringLiteral("clean-id"), QStringLiteral("dirty-id")},
                                QStringLiteral("dirty-id"));
        expect(writer.flush() == CheckpointStatus::Durable,
               "persist duplicate-path clean and dirty fixture");
    }

    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->resize(900, 500);
    window->show();
    processFor(40);
    auto *primary = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    auto *secondary = window->findChild<QTabWidget *>(QStringLiteral("secondaryTabWidget"));
    auto *splitter = window->findChild<QSplitter *>(QStringLiteral("editorViewSplitter"));
    actionWithText(window.get(), QStringLiteral("Clone to Other View"))->trigger();
    splitter->setSizes({320, 580});
    processFor(30);
    const QList<int> originalSizes = splitter->sizes();
    QWidget *cleanView = primary->widget(0);
    QWidget *dirtyView = primary->widget(1);
    QWidget *dirtyClone = secondary->widget(0);
    int decisions = 0;
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        ++decisions;
        return CloseDecision::Cancel;
    });
    QString error;
    expect(!deleteFileInMainWindow(window.get(), path, &error),
           "Cancel aborts duplicate-path deletion");
    expect(decisions == 1, "duplicate-path deletion prompts once for the dirty logical document");
    expect(QFileInfo::exists(path) && primary->count() == 2 && secondary->count() == 1 &&
               primary->widget(0) == cleanView && primary->widget(1) == dirtyView &&
               secondary->widget(0) == dirtyClone && splitter->sizes() == originalSizes,
           "Cancel preserves every matching logical document view and layout");
    expect(EditorUtils::text(dirtyView->findChild<ScintillaEditBase *>()) ==
               QByteArrayLiteral("unsaved duplicate content") &&
               EditorUtils::text(dirtyClone->findChild<ScintillaEditBase *>()) ==
               QByteArrayLiteral("unsaved duplicate content") &&
               window->findChild<SessionManager *>()->documents().size() == 2,
           "Cancel preserves duplicate-path content and session records");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteMultipleDirtyLogicalDocumentsPreflightsCancelBeforeSave()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("multiple-dirty-cancel.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create multiple-dirty Cancel fixture");
    file.write("disk remains original");
    file.close();
    {
        SessionManager writer(nullptr, sessionDirectory.path(), 20);
        writer.loadSession();
        writer.updateDocument({QStringLiteral("dirty-a"), path, 0, true},
                              QByteArrayLiteral("unsaved A"));
        writer.updateDocument({QStringLiteral("dirty-b"), path, 0, true},
                              QByteArrayLiteral("unsaved B"));
        writer.setSessionLayout({QStringLiteral("dirty-a"), QStringLiteral("dirty-b")},
                                QStringLiteral("dirty-a"));
        expect(writer.flush() == CheckpointStatus::Durable,
               "persist multiple-dirty Cancel fixture");
    }

    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    int decisions = 0;
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        return ++decisions == 1 ? CloseDecision::Save : CloseDecision::Cancel;
    });
    QString error;
    expect(!deleteFileInMainWindow(window.get(), path, &error) && decisions == 2,
           "multiple dirty duplicate-path documents are all preflighted before cancellation");
    QFile unchanged(path);
    expect(unchanged.open(QIODevice::ReadOnly) && unchanged.readAll() == "disk remains original",
           "later Cancel prevents an earlier staged Save from mutating the file");
    expect(tabs->count() == 2 && tabs->tabText(0).endsWith('*') &&
               tabs->tabText(1).endsWith('*') &&
               EditorUtils::text(tabs->widget(0)->findChild<ScintillaEditBase *>()) == "unsaved A" &&
               EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) == "unsaved B" &&
               window->findChild<SessionManager *>()->documents().size() == 2,
           "multiple-dirty Cancel preserves all views, buffers, dirty state, and session records");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteDuplicatePathSaveFailureIsAtomic()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("duplicate-save-failure.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create duplicate-path Save failure fixture");
    file.write("baseline");
    file.close();
    {
        SessionManager writer(nullptr, sessionDirectory.path(), 20);
        writer.loadSession();
        writer.updateDocument({QStringLiteral("save-a"), path, 0, true}, QByteArrayLiteral("save A"));
        writer.updateDocument({QStringLiteral("save-b"), path, 0, true}, QByteArrayLiteral("save B"));
        writer.setSessionLayout({QStringLiteral("save-a"), QStringLiteral("save-b")},
                                QStringLiteral("save-a"));
        expect(writer.flush() == CheckpointStatus::Durable,
               "persist duplicate-path Save failure fixture");
    }
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    file.setFileName(path);
    expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "externally modify duplicate-path Save failure fixture");
    file.write("external version");
    file.close();
    int decisions = 0;
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        ++decisions;
        return CloseDecision::Save;
    });
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict, bool) {
        return ExternalSaveDecision::Cancel;
    });
    QString error;
    expect(!deleteFileInMainWindow(window.get(), path, &error) && decisions == 2,
           "duplicate-path Save cancellation aborts deletion after complete decision preflight");
    QFile unchanged(path);
    expect(unchanged.open(QIODevice::ReadOnly) && unchanged.readAll() == "external version",
           "failed duplicate-path Save preserves the external file");
    expect(tabs->count() == 2 && tabs->tabText(0).endsWith('*') &&
               tabs->tabText(1).endsWith('*') &&
               EditorUtils::text(tabs->widget(0)->findChild<ScintillaEditBase *>()) == "save A" &&
               EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) == "save B" &&
               window->findChild<SessionManager *>()->documents().size() == 2,
           "failed duplicate-path Save preserves all logical documents atomically");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteDuplicatePathLaterSaveFailureRollsBackEarlierSave()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("later-save-failure.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create later Save failure fixture");
    file.write("original before saves");
    file.close();
    {
        SessionManager writer(nullptr, sessionDirectory.path(), 20);
        writer.loadSession();
        writer.updateDocument({QStringLiteral("later-save-a"), path, 0, true},
                              QByteArrayLiteral("first staged save"));
        writer.updateDocument({QStringLiteral("later-save-b"), path, 0, true},
                              QByteArrayLiteral("second staged save"));
        writer.setSessionLayout({QStringLiteral("later-save-a"), QStringLiteral("later-save-b")},
                                QStringLiteral("later-save-a"));
        expect(writer.flush() == CheckpointStatus::Durable,
               "persist later Save failure fixture");
    }
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *tabs = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [](const QString &) { return CloseDecision::Save; });
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict, bool) {
        return ExternalSaveDecision::Cancel;
    });
    QString error;
    expect(!deleteFileInMainWindow(window.get(), path, &error),
           "later duplicate-path Save failure aborts deletion");
    QFile unchanged(path);
    expect(unchanged.open(QIODevice::ReadOnly) && unchanged.readAll() == "original before saves",
           "later Save failure rolls back an earlier Save's file mutation");
    expect(tabs->count() == 2 && tabs->tabText(0).endsWith('*') &&
               tabs->tabText(1).endsWith('*') &&
               EditorUtils::text(tabs->widget(0)->findChild<ScintillaEditBase *>()) ==
                   "first staged save" &&
               EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) ==
                   "second staged save" &&
               window->findChild<SessionManager *>()->documents().size() == 2,
           "later Save failure restores every logical document's dirty recoverable state");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}

void testDeleteMultipleDirtyLogicalDocumentsSaveDiscardClosesEverything()
{
    QTemporaryDir settingsDirectory;
    QTemporaryDir files;
    QTemporaryDir sessionDirectory;
    const QByteArray previousSession = qgetenv("NPP_SESSION_DIR");
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().clear();
    const QString path = files.filePath(QStringLiteral("multiple-dirty-success.txt"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create multiple-dirty success fixture");
    file.write("baseline");
    file.close();
    {
        SessionManager writer(nullptr, sessionDirectory.path(), 20);
        writer.loadSession();
        writer.updateDocument({QStringLiteral("success-a"), path, 0, true}, QByteArrayLiteral("save me"));
        writer.updateDocument({QStringLiteral("success-b"), path, 0, true}, QByteArrayLiteral("discard me"));
        writer.setSessionLayout({QStringLiteral("success-a"), QStringLiteral("success-b")},
                                QStringLiteral("success-a"));
        expect(writer.flush() == CheckpointStatus::Durable,
               "persist multiple-dirty success fixture");
    }
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show();
    processFor(30);
    auto *primary = window->findChild<QTabWidget *>(QStringLiteral("primaryTabWidget"));
    auto *secondary = window->findChild<QTabWidget *>(QStringLiteral("secondaryTabWidget"));
    actionWithText(window.get(), QStringLiteral("Clone to Other View"))->trigger();
    int decisions = 0;
    setFileDeleteDecisionProvider(window.get(), [](const QString &) { return true; });
    setCloseDecisionProvider(window.get(), [&](const QString &) {
        return ++decisions == 1 ? CloseDecision::Save : CloseDecision::Discard;
    });
    QString error;
    expect(deleteFileInMainWindow(window.get(), path, &error) && !QFileInfo::exists(path) &&
               decisions == 2,
           "Save and Discard complete duplicate-path deletion after one prompt per logical document");
    expect(primary->count() + secondary->count() == 1 && secondary->count() == 0,
           "successful duplicate-path deletion closes every logical document view");
    bool stalePath = false;
    for (const RecoveryDocument &document : window->findChild<SessionManager *>()->documents())
        stalePath = stalePath || QFileInfo(document.filePath).absoluteFilePath() ==
                                     QFileInfo(path).absoluteFilePath();
    expect(!stalePath, "no surviving session record references the deleted path");
    window.reset();
    qputenv("NPP_SESSION_DIR", previousSession);
}
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir sessionDirectory;
    qputenv("NPP_SESSION_DIR", sessionDirectory.path().toUtf8());
    testReplaceAll();
    testExactBackgroundDocumentWrites();
    testFunctionParsing();
    testDocumentOverviewCoordinatesAndClick();
    testWrappedAndFoldedViewportConversion();
    testDocumentListActivation();
    testFileBrowserActivation();
    testMainWindowPanelOrchestrationAndUpdates();
    testMainWindowFileActivationBackgroundSaveAndSaveAsCancel();
    testOpenFileRenameUpdatesLogicalPathAndLanguage();
    testDocumentListRoutesMainWindowDocumentCommands();
    testDocumentListBulkCloseCancelIsAtomic();
    testDocumentListClonedDirtyCloseCancelPreservesBothViews();
    testDocumentListClonedDirtyCloseSaveAndDiscardCloseAllViews();
    testFileBrowserDeleteConfirmationAndDirtySafety();
    testDeleteHandlesDirtySaveAndConfirmationReplacement();
    testDeleteRejectsPathReplacementAfterDirtyDecision();
    testDeleteRejectsPathReplacementImmediatelyAfterSave();
    testDeleteCleanAndDirtyLogicalDocumentsAtSamePathCancelsAtomically();
    testDeleteMultipleDirtyLogicalDocumentsPreflightsCancelBeforeSave();
    testDeleteDuplicatePathSaveFailureIsAtomic();
    testDeleteDuplicatePathLaterSaveFailureRollsBackEarlierSave();
    testDeleteMultipleDirtyLogicalDocumentsSaveDiscardClosesEverything();
    if (failures == 0)
        std::puts("All Linux panel tests passed");
    return failures == 0 ? 0 : 1;
}
