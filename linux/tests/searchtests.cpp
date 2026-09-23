#include "searchmanager.h"
#include "editorutils.h"
#include "ScintillaEditBase.h"
#include "findreplace.h"
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <type_traits>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <QPointer>
#include <cstdio>

namespace {
int failures;
void expect(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
void setText(ScintillaEditBase &editor, const QByteArray &text) {
    editor.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
    editor.send(SCI_EMPTYUNDOBUFFER);
    editor.send(SCI_SETSAVEPOINT);
}
SearchRequest request(QString pattern, SearchMode mode = SearchMode::Normal) {
    SearchRequest value; value.pattern = std::move(pattern); value.mode = mode; return value;
}

void testFindDirectionsOptionsAndUtf8() {
    ScintillaEditBase editor;
    setText(editor, QStringLiteral("cat scatter Cat cat café λ 日本語 😀 cat").toUtf8());
    SearchManager manager;
    auto r = request(QStringLiteral("cat")); r.options.wholeWord = true;
    r.range = {1, editor.send(SCI_GETTEXTLENGTH)};
    auto hit = manager.find(&editor, r);
    expect(hit && hit->start == 12 && hit->length == 3, "forward whole-word search respects start");
    r.options.matchCase = true; r.direction = SearchDirection::Backward;
    r.range = {editor.send(SCI_GETTEXTLENGTH), 0};
    hit = manager.find(&editor, r);
    expect(hit && hit->start == editor.send(SCI_GETTEXTLENGTH) - 3, "true reverse target finds last match");
    auto unicode = request(QStringLiteral("日本語"));
    unicode.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    hit = manager.find(&editor, unicode);
    expect(hit && hit->length == QStringLiteral("日本語").toUtf8().size(), "UTF-8 hit length is bytes");
    auto wrapped = manager.findNext(&editor, r, 0, true);
    expect(wrapped && wrapped->start == editor.send(SCI_GETTEXTLENGTH) - 3 && wrapped->wrapped,
           "backward search wraps exactly once");
}

void testExtendedAndRegex() {
    ScintillaEditBase editor; SearchManager manager;
    setText(editor, "a\nb\t\\ A1 B2");
    auto ext = request(QStringLiteral("a\\nb\\t\\\\"), SearchMode::Extended);
    ext.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    expect(manager.find(&editor, ext).has_value(), "extended escapes decode");
    QString error;
    expect(!SearchManager::compilePattern(QStringLiteral("\\xG1"), SearchMode::Extended, &error).has_value() && !error.isEmpty(),
           "malformed extended hex is rejected");
    auto regex = request(QStringLiteral("([A-Z])(\\d)"), SearchMode::Regex);
    regex.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    auto hit = manager.find(&editor, regex);
    expect(hit && hit->start == 6, "C++11 regex finds capture input");
    editor.send(SCI_SETSEL, hit->start, hit->end());
    expect(manager.replaceCurrent(&editor, regex, QStringLiteral("\\2\\1")), "regex selected match replaced");
    expect(EditorUtils::text(&editor).contains("1A"), "regex capture replacement uses REPLACETARGETRE");

    setText(editor, "ab");
    for (const QString &invalidPattern : {QStringLiteral("("), QStringLiteral("(?<=a)b")}) {
        auto invalid = request(invalidPattern, SearchMode::Regex);
        invalid.range = {0, 2};
        QString regexError;
        expect(!manager.find(&editor, invalid, &regexError) &&
                   regexError.contains(QStringLiteral("C++11")),
               "Scintilla C++11 regex syntax errors are reported by find");
        regexError.clear();
        expect(manager.count(&editor, invalid, &regexError) == 0 &&
                   regexError.contains(QStringLiteral("C++11")),
               "Scintilla C++11 regex syntax errors are reported by count");
        const auto invalidReplace = manager.replaceAll(&editor, invalid, QStringLiteral("x"));
        expect(invalidReplace.count == 0 && invalidReplace.error.contains(QStringLiteral("C++11")),
               "Scintilla C++11 regex syntax errors are reported by replace-all");
    }

    setText(editor, "x\\n");
    auto escaped = request(QStringLiteral("\\\\n"), SearchMode::Extended); escaped.range = {0, 3};
    editor.send(SCI_SETSEL, 1, 3);
    expect(manager.replaceCurrent(&editor, escaped, QStringLiteral("\\n")) && EditorUtils::text(&editor) == "x\n",
           "extended replacement decodes escapes");
}

void testReplaceValidationNextAndAll() {
    ScintillaEditBase editor; SearchManager manager;
    setText(editor, QStringLiteral("café x café aaa").toUtf8());
    auto r = request(QStringLiteral("café")); r.options.matchCase = true;
    r.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    editor.send(SCI_SETSEL, 0, 3);
    expect(!manager.replaceCurrent(&editor, r, QStringLiteral("λ")), "partial UTF-8 selection is not replaced");
    editor.send(SCI_SETSEL, 0, 5);
    expect(manager.replaceCurrent(&editor, r, QStringLiteral("λ")), "exact current match is replaced");
    expect(EditorUtils::text(&editor).startsWith(QStringLiteral("λ x").toUtf8()), "UTF-8 replacement uses byte length");

    setText(editor, "aaa");
    auto aa = request(QStringLiteral("a")); aa.range = {0, 3};
    ReplaceAllResult result = manager.replaceAll(&editor, aa, QStringLiteral("aa"));
    expect(result.count == 3 && EditorUtils::text(&editor) == "aaaaaa", "replace all does not rescan inserted text");
    expect(editor.send(SCI_CANUNDO), "replace all is one undoable transaction");
    editor.send(SCI_UNDO);
    expect(EditorUtils::text(&editor) == "aaa", "one undo reverts all replacements");
    setText(editor, "aaaa"); aa.pattern = QStringLiteral("aa"); aa.range = {1, 3};
    result = manager.replaceAll(&editor, aa, QStringLiteral("x"));
    expect(result.count == 1 && EditorUtils::text(&editor) == "axa", "selection replace-all is bounded and shrinking safe");
}

void testSelectionBoundedInteractiveSearch() {
    ScintillaEditBase editor; SearchManager manager;
    setText(editor, "out hit 12345678 hit out");
    auto r = request(QStringLiteral("hit")); r.range = {4, 20};
    auto hit = manager.findNext(&editor, r, 0, false);
    expect(hit && hit->start == 4, "selection next clamps a caret before the original range");
    hit = manager.findNext(&editor, r, 19, false);
    expect(!hit, "selection next without wrap cannot escape the original range");
    hit = manager.findNext(&editor, r, 19, true);
    expect(hit && hit->start == 4 && hit->wrapped, "selection next wraps within original bounds");

    r.direction = SearchDirection::Backward;
    hit = manager.findNext(&editor, r, 27, false);
    expect(hit && hit->start == 17, "selection previous clamps a caret after the original range");
    hit = manager.findNext(&editor, r, 4, false);
    expect(!hit, "selection previous without wrap cannot escape the original range");
    hit = manager.findNext(&editor, r, 4, true);
    expect(hit && hit->start == 17 && hit->wrapped, "selection previous wraps within original bounds");

    r.direction = SearchDirection::Forward;
    editor.send(SCI_SETSEL, 4, 7);
    hit = manager.replaceNext(&editor, r, QStringLiteral("longer"), true);
    expect(EditorUtils::text(&editor) == "out longer 12345678 hit out",
           "selection replace changes only the selected hit");
    expect(hit && hit->start == 20,
           "selection replace keeps a stable adjusted range after growth");
}

void testSearchWorkIsLinearInResults() {
    ScintillaEditBase editor;
    QByteArray dense(20000, 'a');
    setText(editor, dense);
    SearchWorkStats stats;
    SearchManager manager(&stats);
    auto literal = request(QStringLiteral("a"));
    literal.range = {0, dense.size()};

    expect(manager.findAll(&editor, literal, QStringLiteral("dense"), {}, {}).size() == dense.size(),
           "dense find-all returns every match");
    expect(stats.contentSnapshots == 1 && stats.revisionHashes == 1,
           "find-all snapshots and hashes a dense document once");
    expect(stats.resultItems == dense.size(),
           "find-all performs one bounded result construction per match");
    expect(stats.columnBytesDecoded <= dense.size(),
           "find-all decodes at most one document worth of bytes for result columns");
    expect(stats.previewBytesCopied <= 500,
           "find-all builds a dense single-line preview once");

    stats = {};
    expect(manager.count(&editor, literal) == dense.size(), "streaming count returns every dense match");
    expect(stats.contentSnapshots == 0 && stats.revisionHashes == 0 && stats.resultItems == 0,
           "count streams without snapshots, revision hashes, or result allocations");

    stats = {};
    auto zero = request(QStringLiteral("(?=a)"), SearchMode::Regex);
    zero.range = {0, dense.size()};
    expect(manager.count(&editor, zero) == dense.size(), "streaming count includes zero-length matches");
    expect(stats.contentSnapshots == 0 && stats.revisionHashes == 0,
           "zero-length count remains streaming");

    stats = {};
    expect(manager.markAll(&editor, literal) == dense.size(), "streaming mark covers every dense match");
    expect(stats.contentSnapshots == 0 && stats.revisionHashes == 0 && stats.resultItems == 0,
           "mark-all streams without constructing search results");
}

void testCountFindAllAndMark() {
    ScintillaEditBase editor; SearchManager manager;
    setText(editor, QStringLiteral("λ one\nλ two\n").toUtf8());
    auto r = request(QStringLiteral("λ")); r.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    const QByteArray before = EditorUtils::text(&editor);
    expect(manager.count(&editor, r) == 2 && EditorUtils::text(&editor) == before, "count is nonmutating");
    auto hits = manager.findAll(&editor, r, QStringLiteral("doc-1"), QStringLiteral("name"), QStringLiteral("/tmp/name"));
    expect(hits.size() == 2 && hits[1].line == 2 && hits[1].column == 1 && hits[1].documentId == "doc-1" && hits[0].length == 2,
           "find-all metadata has logical id, line, column, UTF-8 byte range");
    expect(hits[0].matchedBytes == QStringLiteral("λ").toUtf8() && !hits[0].contentRevision.isEmpty(),
           "search results own stable matched bytes and a value revision");

    setText(editor, QStringLiteral("éx λ\n😀x λ").toUtf8());
    auto x = request(QStringLiteral("x")); x.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    const auto unicodeColumns = manager.findAll(&editor, x, {}, {}, {});
    expect(unicodeColumns.size() == 2 && unicodeColumns[0].column == 2 &&
               unicodeColumns[1].column == 2,
           "result columns count Unicode code points, including emoji, not UTF-8 bytes");

    setText(editor, before);
    expect(SearchManager::resultStillValid(&editor, hits[0]), "fresh search result validates");
    setText(editor, QStringLiteral("μ one\nλ two\n").toUtf8());
    expect(!SearchManager::resultStillValid(&editor, hits[0]),
           "same-length replacement invalidates a saved result");
    setText(editor, QStringLiteral("xλ one\nλ two\n").toUtf8());
    expect(!SearchManager::resultStillValid(&editor, hits[1]),
           "insertion before a hit invalidates its saved offset");
    setText(editor, before);
    expect(manager.markAll(&editor, r) == 2, "mark all marks every UTF-8 match");
    expect(editor.send(SCI_GETMODIFY) == 0, "marking does not dirty document");
    manager.clearMarks(&editor);
    expect(editor.send(SCI_GETMODIFY) == 0, "clearing marks does not dirty document");
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path); expect(file.open(QIODevice::WriteOnly), "test fixture file opens"); file.write(bytes);
}
void testFindInFiles() {
    QTemporaryDir temp; QDir dir(temp.path()); dir.mkpath("sub");
    writeFile(dir.filePath("u8.txt"), QStringLiteral("café\n").toUtf8());
    writeFile(dir.filePath("sub/u16.txt"), QByteArray::fromHex("fffe630061006600e9000a00"));
    writeFile(dir.filePath("skip.bin"), QByteArray("a\0café", 7));
    writeFile(dir.filePath("skip.log"), "caf\xc3\xa9");
    writeFile(dir.filePath("cp.txt"), QByteArray("caf\xe9\n", 5));
    QFile::link(temp.path(), dir.filePath("sub/loop"));
    QTemporaryDir outside;
    writeFile(QDir(outside.path()).filePath("secret.txt"), "cafe\n");
    expect(QFile::link(QDir(outside.path()).filePath("secret.txt"), dir.filePath("linked.txt")),
           "outside-file symlink fixture is created");
    FileSearchRequest r; r.pattern = QStringLiteral("café"); r.directory = temp.path();
    r.filters = QStringLiteral("*.txt"); r.recursive = true;
    const FileSearchResult result = SearchManager().findInFiles(r);
    expect(result.matches.size() == 3, "find in files decodes UTF-8, UTF-16, and CP1252 and applies filters");
    expect(result.skippedBinary == 0 && result.errors.size() < 3, "binary/symlink handling is bounded");

    FileSearchRequest links = r; links.pattern = QStringLiteral("cafe");
    const FileSearchResult linked = SearchManager().findInFiles(links);
    expect(linked.matches.isEmpty(), "find in files skips file symlinks whose target is outside the tree");

    FileSearchRequest bounded = r; bounded.maximumFileBytes = 4;
    const FileSearchResult oversized = SearchManager().findInFiles(bounded);
    expect(oversized.matches.isEmpty() && oversized.skippedUnreadable == 3 &&
               oversized.errors.size() == 3,
           "file size limits are enforced and surfaced");
    FileSearchRequest binary = r; binary.filters = QStringLiteral("*.bin");
    const FileSearchResult binaryResult = SearchManager().findInFiles(binary);
    expect(binaryResult.matches.isEmpty() && binaryResult.skippedBinary == 1,
           "binary files are explicitly skipped");

    QTemporaryDir encodedTemp; QDir encoded(encodedTemp.path());
    writeFile(encoded.filePath("bom.txt"), QByteArray::fromHex("efbbbff09f988041310a"));
    writeFile(encoded.filePath("be.txt"), QByteArray::fromHex("feff00420032000a"));
    FileSearchRequest encodedRegex;
    encodedRegex.pattern = QStringLiteral("[A-B]\\d");
    encodedRegex.mode = SearchMode::Regex;
    encodedRegex.directory = encodedTemp.path();
    const FileSearchResult encodedResult = SearchManager().findInFiles(encodedRegex);
    expect(encodedResult.errors.isEmpty() && encodedResult.matches.size() == 2,
           "file regex search covers UTF-8 BOM and UTF-16BE documents");
    const auto bomMatch = std::find_if(encodedResult.matches.cbegin(), encodedResult.matches.cend(),
        [](const SearchResultItem &item) { return item.name == QStringLiteral("bom.txt"); });
    expect(bomMatch != encodedResult.matches.cend() && bomMatch->start == 4 && bomMatch->column == 2,
           "file regex offsets remain UTF-8 bytes after a supplementary Unicode code point");

    writeFile(encoded.filePath("anchors.txt"), "x\nx\n");
    FileSearchRequest anchors = encodedRegex;
    anchors.pattern = QStringLiteral("^x$");
    const FileSearchResult anchorResult = SearchManager().findInFiles(anchors);
    expect(anchorResult.matches.size() == 2,
           "file regex anchors retain Scintilla line-by-line semantics without suffix rebasing");

    FileSearchRequest totalLimited = r;
    totalLimited.maximumTotalBytes = 6;
    const FileSearchResult totalResult = SearchManager().findInFiles(totalLimited);
    expect(totalResult.limited && totalResult.bytesScanned <= 6,
           "file search enforces an aggregate byte cap");
}

void testRegexReplaceAllAndNoMatchUndo() {
    ScintillaEditBase editor; SearchManager manager;
    setText(editor, "A1 B2");
    auto regex = request(QStringLiteral("([A-Z])(\\d)"), SearchMode::Regex);
    regex.range = {0, editor.send(SCI_GETTEXTLENGTH)};
    const auto replaced = manager.replaceAll(&editor, regex, QStringLiteral("\\2-λ\\1"));
    expect(replaced.count == 2 && EditorUtils::text(&editor) == QStringLiteral("1-λA 2-λB").toUtf8(),
           "regex replace all expands captures with UTF-8 bytes");
    setText(editor, "unchanged");
    auto missing = request(QStringLiteral("absent")); missing.range = {0, 9};
    const auto noMatch = manager.replaceAll(&editor, missing, QStringLiteral("x"));
    expect(noMatch.count == 0 && !editor.send(SCI_CANUNDO),
           "no-match replace all creates no undo transaction");
}

void testZeroLengthAndCloneDedupe() {
    ScintillaEditBase first, clone; SearchManager manager; setText(first, "ab");
    clone.send(SCI_SETDOCPOINTER, 0, first.send(SCI_GETDOCPOINTER));
    auto regex = request(QStringLiteral("(?=.)"), SearchMode::Regex); regex.range = {0, 2};
    expect(manager.count(&first, regex) == 2, "zero-length regex progresses safely");
    auto zero = manager.findNext(&first, regex, 0, false);
    auto nextZero = manager.findNext(&first, regex, zero->start, false);
    expect(zero && nextZero && zero->start == 0 && nextZero->start == 1,
           "repeated next skips the same zero-length match");
    auto literalAfterZero = request(QStringLiteral("a")); literalAfterZero.range = {0, 2};
    auto literalHit = manager.findNext(&first, literalAfterZero, 0, false);
    expect(literalHit && literalHit->start == 0,
           "changing query after a zero-length hit does not skip the first literal match");
    auto otherZero = request(QStringLiteral("(?=a)"), SearchMode::Regex); otherZero.range = {0, 2};
    auto changedZero = manager.findNext(&first, otherZero, 0, false);
    expect(changedZero && changedZero->start == 0,
           "changing zero-length query resets repeat suppression");
    otherZero.direction = SearchDirection::Backward; otherZero.range = {2, 0};
    auto reversed = manager.findNext(&first, otherZero, 0, true);
    expect(reversed && reversed->start == 0,
           "changing direction resets zero-length repeat suppression");
    setText(first, "aa");
    otherZero.direction = SearchDirection::Forward; otherZero.range = {0, 2};
    auto afterEdit = manager.findNext(&first, otherZero, 0, false);
    expect(afterEdit && afterEdit->start == 0,
           "document revision resets zero-length repeat suppression");
    setText(first, "ab");
    QVector<OpenSearchDocument> docs{{"same", {}, "one", &first}, {"same", {}, "clone", &clone}};
    auto literal = request(QStringLiteral("a")); literal.range = {0, 2};
    expect(manager.findAllOpen(docs, literal).size() == 1, "all-open search deduplicates clones by logical id");

    ScintillaEditBase shortEditor, longEditor;
    setText(shortEditor, "a"); setText(longEditor, "----a");
    QVector<OpenSearchDocument> unequal{{"short", {}, "short", &shortEditor},
                                        {"long", {}, "long", &longEditor}};
    literal.range = {0, 1};
    const auto unequalHits = manager.findAllOpen(unequal, literal);
    expect(unequalHits.size() == 2 && unequalHits[1].start == 4,
           "find all open uses each document's full byte range");
}

void testDialogAndWindowIntegration() {
    QTemporaryDir settingsDir; qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    FindReplaceDialog dialog; auto *findBox = dialog.findChild<QComboBox *>("findTextCombo");
    auto *replaceBox = dialog.findChild<QComboBox *>("replaceTextCombo");
    for (int i = 0; i < 20; ++i) { dialog.showFind(); dialog.showReplace(); dialog.showFindInFiles(); dialog.showMark(); }
    expect(findBox && replaceBox && dialog.currentPage() == FindReplaceDialog::MarkPage,
           "repeated page switching preserves shared widget lifetime");
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *find = window->findChild<QAction *>("findAction"); auto *replace = window->findChild<QAction *>("replaceAction");
    auto *next = window->findChild<QAction *>("findNextAction"); auto *previous = window->findChild<QAction *>("findPreviousAction");
    expect(find && find->shortcut() == QKeySequence::Find && replace && replace->shortcut() == QKeySequence("Ctrl+H"),
           "find and replace shortcuts are wired");
    expect(next && next->shortcut() == QKeySequence(Qt::Key_F3) && previous && previous->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_F3),
           "next and previous shortcuts are wired");
    for (const char *name : {"countAction", "findAllCurrentAction", "findAllOpenAction",
                             "replaceAllAction", "clearMarksAction"})
        expect(window->findChild<QAction *>(name), "full Search menu action is present and named");
    auto *countButton = dialog.findChild<QPushButton *>("countButton");
    auto *nextButton = dialog.findChild<QPushButton *>("findNextButton");
    dialog.setFindText({}); QApplication::processEvents();
    expect(countButton && !countButton->isEnabled() && nextButton && !nextButton->isEnabled(),
           "pattern-dependent search actions are disabled for an empty pattern");
    find->trigger(); QApplication::processEvents();
    expect(window->findChild<FindReplaceDialog *>("findReplaceDialog")->currentPage() == FindReplaceDialog::FindPage,
           "search action opens stable Find page");
}

void testSearchHistoryPersistenceAndCommandPaths() {
    QTemporaryDir settingsDir;
    qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings().clear();
    {
        FindReplaceDialog dialog;
        dialog.findChild<QComboBox *>("findTextCombo")->setCurrentText(QStringLiteral("draft find"));
        dialog.findChild<QComboBox *>("replaceTextCombo")->setCurrentText(QStringLiteral("draft replace"));
    }
    {
        FindReplaceDialog recreated;
        expect(recreated.findText() == QStringLiteral("draft find") &&
                   recreated.replaceText() == QStringLiteral("draft replace"),
               "uncommitted editable search values survive dialog recreation");
    }

    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *dialog = window->findChild<FindReplaceDialog *>("findReplaceDialog");
    auto *findBox = dialog->findChild<QComboBox *>("findTextCombo");
    findBox->setCurrentText(QStringLiteral("from F3"));
    window->findChild<QAction *>("findNextAction")->trigger();
    findBox->setCurrentText(QStringLiteral("from menu"));
    window->findChild<QAction *>("countAction")->trigger();
    const QStringList history = QSettings().value(QStringLiteral("search/findHistory")).toStringList();
    expect(history.value(0) == QStringLiteral("from menu") && history.contains(QStringLiteral("from F3")),
           "menu and F3 commands commit bounded deduplicated search history");
}

void waitFor(const std::function<bool()> &condition, int timeoutMs = 5000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
}

void testAsyncFindInFilesLifecycle() {
    QTemporaryDir temp;
    writeFile(QDir(temp.path()).filePath("one.txt"), "needle\n");
    writeFile(QDir(temp.path()).filePath("two.txt"), "other\n");
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *dialog = window->findChild<FindReplaceDialog *>("findReplaceDialog");
    auto *button = dialog->findChild<QPushButton *>("findInFilesButton");
    auto *tree = window->findChild<QTreeWidget *>("searchResultsTree");

    bool eventLoopResponsive = false;
    QTimer::singleShot(0, [&] { eventLoopResponsive = true; });
    auto firstEntered = std::make_shared<std::atomic_bool>(false);
    auto releaseFirst = std::make_shared<std::atomic_bool>(false);
    FileSearchRequest first;
    first.pattern = QStringLiteral("needle");
    first.directory = temp.path();
    first.progress = [firstEntered, releaseFirst](int, qint64) {
        firstEntered->store(true);
        while (!releaseFirst->load()) QThread::msleep(1);
    };
    startFileSearchInMainWindow(window.get(), first);
    expect(button && !button->isEnabled(), "find-in-files control disables while background work runs");
    waitFor([&] { return eventLoopResponsive && firstEntered->load(); });
    expect(eventLoopResponsive && firstEntered->load(),
           "find-in-files leaves the GUI event loop responsive while worker progress is blocked");

    FileSearchRequest latest = first;
    latest.pattern = QStringLiteral("other");
    latest.progress = {};
    startFileSearchInMainWindow(window.get(), latest);
    releaseFirst->store(true);
    waitFor([&] { return button->isEnabled(); });
    expect(button->isEnabled() && tree->topLevelItemCount() == 1 &&
               tree->topLevelItem(0)->text(3).contains(QStringLiteral("other")),
           "latest file-search completion suppresses stale cancelled results");

    auto destructionEntered = std::make_shared<std::atomic_bool>(false);
    auto releaseDestruction = std::make_shared<std::atomic_bool>(false);
    FileSearchRequest destruction;
    destruction.pattern = QStringLiteral("needle");
    destruction.directory = temp.path();
    destruction.progress = [destructionEntered, releaseDestruction](int, qint64) {
        destructionEntered->store(true);
        while (!releaseDestruction->load()) QThread::msleep(1);
    };
    startFileSearchInMainWindow(window.get(), destruction);
    waitFor([&] { return destructionEntered->load(); });
    QPointer<QMainWindow> destroyedWindow(window.get());
    window.reset();
    QApplication::processEvents();
    releaseDestruction->store(true);
    expect(destroyedWindow.isNull() && QThreadPool::globalInstance()->waitForDone(5000),
           "destroying a window safely cancels and drains in-flight file search");
}

void activateResult(QTreeWidget *tree, int index = 0) {
    QTreeWidgetItem *item = tree->topLevelItem(index);
    QMetaObject::invokeMethod(tree, "itemActivated", Qt::DirectConnection,
                              Q_ARG(QTreeWidgetItem *, item), Q_ARG(int, 0));
}

void testSearchResultNavigationAndClosedDocumentSafety() {
    QTemporaryDir settingsDir;
    qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *tabs = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    auto *dialog = window->findChild<FindReplaceDialog *>("findReplaceDialog");
    auto *tree = window->findChild<QTreeWidget *>("searchResultsTree");
    setText(*editor, "zero hit end");
    dialog->setFindText(QStringLiteral("hit"));
    dialog->findChild<QPushButton *>("findAllCurrentButton")->click();
    expect(tree->topLevelItemCount() == 1, "find-all publishes an activatable open-document result");
    activateResult(tree);
    expect(editor->send(SCI_GETSELECTIONSTART) == 5 && editor->send(SCI_GETSELECTIONEND) == 8,
           "activating an open-document result navigates to its byte range");

    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 0));
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    activateResult(tree);
    expect(window->statusBar()->currentMessage().contains(QStringLiteral("no longer open")),
           "activating a result for a closed document fails safely");

    QTemporaryDir files;
    const QString path = QDir(files.path()).filePath("result.txt");
    writeFile(path, QStringLiteral("éx file hit").toUtf8());
    FileSearchRequest request;
    request.pattern = QStringLiteral("hit");
    request.directory = files.path();
    startFileSearchInMainWindow(window.get(), request);
    auto *button = dialog->findChild<QPushButton *>("findInFilesButton");
    waitFor([&] { return button->isEnabled(); });
    expect(tree->topLevelItemCount() == 1, "file search publishes an activatable file result");
    activateResult(tree);
    auto *opened = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    expect(opened->send(SCI_GETSELECTIONEND) - opened->send(SCI_GETSELECTIONSTART) == 3,
           "activating a file result opens the file and selects the match");
}

void testSearchCommandsFollowFocusedPane() {
    QTemporaryDir settingsDir;
    qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show(); QApplication::processEvents();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *primaryEditor = primary->currentWidget()->findChild<ScintillaEditBase *>();
    setText(*primaryEditor, "primary hit");
    window->findChild<QAction *>("cloneToOtherViewAction")->trigger();
    window->findChild<QAction *>("newAction")->trigger();
    auto *secondaryEditor = secondary->currentWidget()->findChild<ScintillaEditBase *>();
    setText(*secondaryEditor, "secondary hit");
    auto *dialog = window->findChild<FindReplaceDialog *>("findReplaceDialog");
    dialog->setFindText(QStringLiteral("primary"));

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(primaryEditor, &press); QApplication::processEvents();
    window->findChild<QAction *>("findNextAction")->trigger();
    expect(primaryEditor->send(SCI_GETSELECTIONEND) == 7,
           "F3 routes to the primary editor after focus switches panes");

    dialog->setFindText(QStringLiteral("secondary"));
    QApplication::sendEvent(secondaryEditor, &press); QApplication::processEvents();
    window->findChild<QAction *>("findNextAction")->trigger();
    expect(secondaryEditor->send(SCI_GETSELECTIONEND) == 9,
           "F3 routes to the secondary editor after focus switches panes");
}

struct WindowSearchFixture {
    std::unique_ptr<QMainWindow> window{createMainWindow()};
    QTabWidget *tabs = window->findChild<QTabWidget *>("primaryTabWidget");
    FindReplaceDialog *dialog = window->findChild<FindReplaceDialog *>("findReplaceDialog");
    ScintillaEditBase *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    QComboBox *find = dialog->findChild<QComboBox *>("findTextCombo");
    QComboBox *replace = dialog->findChild<QComboBox *>("replaceTextCombo");
    QCheckBox *inSelection = dialog->findChild<QCheckBox *>("inSelectionCheckBox");
    QCheckBox *wrap = dialog->findChild<QCheckBox *>("wrapCheckBox");

    QPushButton *button(const char *name) const {
        return dialog->findChild<QPushButton *>(name);
    }

    void configure(const QByteArray &text, qint64 selectionStart, qint64 selectionEnd,
                   const QString &pattern, const QString &replacement = {}) {
        setText(*editor, text);
        editor->send(SCI_SETSEL, selectionStart, selectionEnd);
        find->setCurrentText(pattern);
        replace->setCurrentText(replacement);
        inSelection->setChecked(true);
        wrap->setChecked(false);
        QApplication::processEvents();
    }
};

void testWindowSelectionFirstActionsAndRepeatedPresses() {
    QTemporaryDir settingsDir;
    qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    WindowSearchFixture next;
    next.configure("out hit gap hit out", 4, 15, QStringLiteral("hit"));
    next.button("findNextButton")->click();
    expect(next.editor->send(SCI_GETSELECTIONSTART) == 4 &&
               next.editor->send(SCI_GETSELECTIONEND) == 7,
           "first window Find Next starts at the original selection beginning");
    next.button("findNextButton")->click();
    expect(next.editor->send(SCI_GETSELECTIONSTART) == 12,
           "separate Find Next presses retain the original selected scope");

    WindowSearchFixture previous;
    previous.configure("out hit gap hit out", 4, 15, QStringLiteral("hit"));
    previous.button("findPreviousButton")->click();
    expect(previous.editor->send(SCI_GETSELECTIONSTART) == 12 &&
               previous.editor->send(SCI_GETSELECTIONEND) == 15,
           "first window Find Previous starts at the original selection end");
    previous.button("findPreviousButton")->click();
    expect(previous.editor->send(SCI_GETSELECTIONSTART) == 4,
           "separate Find Previous presses retain the original selected scope");

    WindowSearchFixture replace;
    replace.configure("out hit gap hit out", 4, 15, QStringLiteral("hit"),
                      QStringLiteral("HIT"));
    replace.button("replaceButton")->click();
    expect(EditorUtils::text(replace.editor) == "out hit gap hit out" &&
               replace.editor->send(SCI_GETSELECTIONSTART) == 4 &&
               replace.editor->send(SCI_GETSELECTIONEND) == 7,
           "first window Replace finds from the original selection beginning");
    replace.button("replaceButton")->click();
    expect(EditorUtils::text(replace.editor) == "out HIT gap hit out" &&
               replace.editor->send(SCI_GETSELECTIONSTART) == 12,
           "separate Replace presses retain the original selected scope");
}

void testWindowReplaceAllPreservesChangingSelectionScope() {
    QTemporaryDir settingsDir;
    qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    WindowSearchFixture fixture;
    fixture.configure("out a a out a", 4, 7, QStringLiteral("a"), QStringLiteral("long"));
    fixture.button("replaceAllButton")->click();
    expect(EditorUtils::text(fixture.editor) == "out long long out a",
           "window Replace All grows only the original selected scope");

    fixture.find->setCurrentText(QStringLiteral("long"));
    fixture.button("countButton")->click();
    expect(fixture.window->statusBar()->currentMessage() == QStringLiteral("2 matches"),
           "Count after growing Replace All retains the adjusted original scope");
    fixture.button("findNextButton")->click();
    expect(fixture.editor->send(SCI_GETSELECTIONSTART) == 4,
           "Find after growing Replace All starts at the adjusted original scope");

    fixture.replace->setCurrentText(QStringLiteral("x"));
    fixture.button("replaceAllButton")->click();
    expect(EditorUtils::text(fixture.editor) == "out x x out a",
           "later Replace All shrinks the full original selected scope");
    fixture.find->setCurrentText(QStringLiteral("x"));
    fixture.button("countButton")->click();
    expect(fixture.window->statusBar()->currentMessage() == QStringLiteral("2 matches"),
           "Count after shrinking Replace All retains the adjusted original scope");
    fixture.button("findNextButton")->click();
    expect(fixture.editor->send(SCI_GETSELECTIONSTART) == 4,
           "Find after shrinking Replace All remains inside the original scope");
}

void testWindowFinalReplacePreservesChangingSelectionScope() {
    QTemporaryDir settingsDir;
    qputenv("NPP_SESSION_DIR", settingsDir.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    const auto replaceFinalMatch = [](WindowSearchFixture &fixture,
                                      const QByteArray &text, qint64 scopeEnd,
                                      const QString &pattern, const QString &replacement) {
        fixture.configure(text, 4, scopeEnd, pattern, replacement);
        fixture.button("replaceButton")->click();
        fixture.button("replaceButton")->click();
        fixture.button("replaceButton")->click();
    };

    WindowSearchFixture growing;
    replaceFinalMatch(growing, "out a mid a out a", 11,
                      QStringLiteral("a"), QStringLiteral("long"));
    expect(EditorUtils::text(growing.editor) == "out long mid long out a",
           "window Replace grows the final match in the selected scope");
    growing.find->setCurrentText(QStringLiteral("long"));
    growing.button("countButton")->click();
    expect(growing.window->statusBar()->currentMessage() == QStringLiteral("2 matches"),
           "Count after a growing final Replace retains the adjusted original scope");
    growing.find->setCurrentText(QStringLiteral("mid"));
    growing.replace->setCurrentText(QStringLiteral("x"));
    growing.wrap->setChecked(true);
    growing.button("findNextButton")->click();
    expect(growing.editor->send(SCI_GETSELECTIONSTART) == 9,
           "Find after a growing final Replace searches the adjusted original scope");
    growing.button("replaceButton")->click();
    expect(EditorUtils::text(growing.editor) == "out long x long out a",
           "Replace after a growing final Replace remains in the adjusted original scope");

    WindowSearchFixture shrinking;
    replaceFinalMatch(shrinking, "out word gap word out word", 17,
                      QStringLiteral("word"), QStringLiteral("x"));
    expect(EditorUtils::text(shrinking.editor) == "out x gap x out word",
           "window Replace shrinks the final match in the selected scope");
    shrinking.find->setCurrentText(QStringLiteral("x"));
    shrinking.button("countButton")->click();
    expect(shrinking.window->statusBar()->currentMessage() == QStringLiteral("2 matches"),
           "Count after a shrinking final Replace retains the adjusted original scope");
    shrinking.find->setCurrentText(QStringLiteral("gap"));
    shrinking.replace->setCurrentText(QStringLiteral("G"));
    shrinking.wrap->setChecked(true);
    shrinking.button("findNextButton")->click();
    expect(shrinking.editor->send(SCI_GETSELECTIONSTART) == 6,
           "Find after a shrinking final Replace searches the adjusted original scope");
    shrinking.button("replaceButton")->click();
    expect(EditorUtils::text(shrinking.editor) == "out x G x out word",
           "Replace after a shrinking final Replace remains in the adjusted original scope");

    shrinking.editor->send(SCI_SETSEL, 14, 18);
    shrinking.find->setCurrentText(QStringLiteral("word"));
    shrinking.button("countButton")->click();
    expect(shrinking.window->statusBar()->currentMessage() == QStringLiteral("1 matches"),
           "a true user selection still establishes a new search scope");
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    testFindDirectionsOptionsAndUtf8();
    testExtendedAndRegex();
    testReplaceValidationNextAndAll();
    testSelectionBoundedInteractiveSearch();
    testSearchWorkIsLinearInResults();
    testCountFindAllAndMark();
    testFindInFiles();
    testRegexReplaceAllAndNoMatchUndo();
    testZeroLengthAndCloneDedupe();
    testDialogAndWindowIntegration();
    testSearchHistoryPersistenceAndCommandPaths();
    testAsyncFindInFilesLifecycle();
    testSearchResultNavigationAndClosedDocumentSafety();
    testSearchCommandsFollowFocusedPane();
    testWindowSelectionFirstActionsAndRepeatedPresses();
    testWindowReplaceAllPreservesChangingSelectionScope();
    testWindowFinalReplacePreservesChangingSelectionScope();
    if (failures) std::fprintf(stderr, "%d search test(s) failed\n", failures);
    return failures ? 1 : 0;
}
