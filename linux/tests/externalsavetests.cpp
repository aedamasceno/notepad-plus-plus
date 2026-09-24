#include "editorutils.h"
#include "mainwindow.h"
#include "sessionmanager.h"
#include "ScintillaEditBase.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>

namespace {
int failures = 0;
void expect(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
void writeBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path); expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture opens");
    expect(file.write(bytes) == bytes.size(), "fixture writes");
}
QByteArray readBytes(const QString &path) {
    QFile file(path); expect(file.open(QIODevice::ReadOnly), "result opens"); return file.readAll();
}
ScintillaEditBase *editor(QMainWindow *window) {
    auto *tabs = window->findChild<QTabWidget *>("primaryTabWidget");
    return tabs->currentWidget()->findChild<ScintillaEditBase *>();
}
void setText(QMainWindow *window, const QByteArray &text) {
    editor(window)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
}
std::unique_ptr<QMainWindow> openWindow(const QString &session, const QString &path) {
    qputenv("NPP_SESSION_DIR", session.toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), path, &error), "named file opens");
    return window;
}

void testUnchangedAndOwnSaveRefreshBaseline() {
    QTemporaryDir root; const QString path = root.filePath("same.txt"); writeBytes(path, "disk");
    auto window = openWindow(root.filePath("session"), path); setText(window.get(), "mine");
    QString error;
    expect(saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == "mine",
           "unchanged file saves normally");
    setText(window.get(), "next");
    expect(saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == "next",
           "successful own save refreshes baseline");
}

void testModifiedDefaultsToCancelAndExplicitOverwrite() {
    QTemporaryDir root; const QString path = root.filePath("modified.txt"); writeBytes(path, "aaaa");
    auto window = openWindow(root.filePath("session"), path); setText(window.get(), "mine");
    const QDateTime originalMtime = QFileInfo(path).lastModified();
    writeBytes(path, "bbbb");
    QFile altered(path);
    expect(altered.open(QIODevice::ReadWrite) &&
               altered.setFileTime(originalMtime, QFileDevice::FileModificationTime),
           "fixture restores the original mtime");
    altered.close();
    QString error;
    expect(!saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == "bbbb" &&
               EditorUtils::text(editor(window.get())) == "mine",
           "without an explicit overwrite decision both disk and editor are preserved");
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict conflict, bool dirty) {
        expect(conflict == ExternalSaveConflict::Modified && dirty, "provider sees modified dirty conflict");
        return ExternalSaveDecision::Overwrite;
    });
    expect(saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == "mine",
           "explicit overwrite atomically replaces external data");
}

void testCancelAndReload() {
    QTemporaryDir root; const QString path = root.filePath("reload.txt"); writeBytes(path, "initial");
    auto window = openWindow(root.filePath("session"), path); setText(window.get(), "dirty buffer");
    writeBytes(path, QByteArray::fromHex("efbbbf") + QByteArray("external\r\n"));
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict, bool) {
        return ExternalSaveDecision::Cancel;
    });
    QString error;
    expect(!saveCurrentFileInMainWindow(window.get(), &error) &&
               EditorUtils::text(editor(window.get())) == "dirty buffer" &&
               readBytes(path).endsWith("external\r\n"),
           "cancel preserves dirty editor and external file");
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict conflict, bool dirty) {
        expect(conflict == ExternalSaveConflict::Modified && dirty, "reload is an explicit dirty discard decision");
        return ExternalSaveDecision::Reload;
    });
    expect(!saveCurrentFileInMainWindow(window.get(), &error) &&
               EditorUtils::text(editor(window.get())) == "external\r\n" &&
               !window->findChild<QTabWidget *>("primaryTabWidget")->tabText(0).endsWith('*') &&
               window->statusBar()->currentMessage().contains("UTF-8 BOM"),
           "reload decodes external bytes and refreshes clean format state without reporting a save");
}

void testDeletedNeedsExplicitRecreate() {
    QTemporaryDir root; const QString path = root.filePath("deleted.txt"); writeBytes(path, "before");
    auto window = openWindow(root.filePath("session"), path); setText(window.get(), "recreated");
    expect(QFile::remove(path), "external deletion succeeds");
    QString error;
    expect(!saveCurrentFileInMainWindow(window.get(), &error) && !QFileInfo::exists(path),
           "deleted destination is never silently recreated");
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict conflict, bool) {
        expect(conflict == ExternalSaveConflict::Deleted, "provider distinguishes deletion");
        return ExternalSaveDecision::Recreate;
    });
    expect(saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == "recreated",
           "explicit recreate writes deleted destination");
}

void testSaveAsInitializesBaselineAndRecoveryRemainsValid() {
    QTemporaryDir root; qputenv("NPP_SESSION_DIR", root.filePath("session").toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow()); setText(window.get(), "saved as");
    const QString path = root.filePath("new name.txt"); QString error;
    expect(saveCurrentFileAsInMainWindow(window.get(), path, &error) && readBytes(path) == "saved as",
           "Save As writes a new path");
    writeBytes(path, "external"); setText(window.get(), "second");
    expect(!saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == "external",
           "Save As initializes baseline for later external-change detection");
    auto *manager = window->findChild<SessionManager *>();
    expect(manager && manager->flush() == CheckpointStatus::Durable &&
               manager->documents().size() == 1 && manager->documents().first().dirty,
           "cancelled conflict leaves valid dirty recovery state");
}

void testSaveAsExistingTargetNeedsExplicitOverwrite() {
    QTemporaryDir root;
    const QString source = root.filePath("source.txt");
    const QString target = root.filePath("valuable.txt");
    writeBytes(source, "source");
    writeBytes(target, "external valuable data");
    auto window = openWindow(root.filePath("session"), source);
    setText(window.get(), "my buffer");
    QString error;
    expect(!saveCurrentFileAsInMainWindow(window.get(), target, &error) &&
               readBytes(target) == "external valuable data" &&
               EditorUtils::text(editor(window.get())) == "my buffer",
           "Save As defaults to cancel and preserves an existing target and editor buffer");
    setExternalSaveDecisionProvider(window.get(), [](ExternalSaveConflict conflict, bool dirty) {
        expect(conflict == ExternalSaveConflict::ExistingSaveAsTarget && dirty,
               "provider sees an existing Save As target");
        return ExternalSaveDecision::Overwrite;
    });
    expect(saveCurrentFileAsInMainWindow(window.get(), target, &error) &&
               readBytes(target) == "my buffer",
           "Save As overwrites an existing target only after an explicit decision");
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    testUnchangedAndOwnSaveRefreshBaseline();
    testModifiedDefaultsToCancelAndExplicitOverwrite();
    testCancelAndReload();
    testDeletedNeedsExplicitRecreate();
    testSaveAsInitializesBaselineAndRecoveryRemainsValid();
    testSaveAsExistingTargetNeedsExplicitOverwrite();
    qunsetenv("NPP_SESSION_DIR");
    return failures == 0 ? 0 : 1;
}
