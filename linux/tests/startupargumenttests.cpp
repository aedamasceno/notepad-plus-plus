#include "mainwindow.h"
#include "startupfiles.h"
#include "editorutils.h"
#include "ScintillaEditBase.h"
#include "sessionmanager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QSet>
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
    QFile file(path); expect(file.open(QIODevice::WriteOnly), "fixture opens");
    expect(file.write(bytes) == bytes.size(), "fixture writes");
}
QTabWidget *tabs(QMainWindow *window) { return window->findChild<QTabWidget *>("primaryTabWidget"); }
QByteArray tabText(QMainWindow *window, int index) {
    return EditorUtils::text(tabs(window)->widget(index)->findChild<ScintillaEditBase *>());
}
std::unique_ptr<QMainWindow> fresh(const QString &session) {
    qputenv("NPP_SESSION_DIR", session.toUtf8()); return std::unique_ptr<QMainWindow>(createMainWindow());
}

void testNoArgsPreservesStartup() {
    QTemporaryDir root; auto window = fresh(root.filePath("session"));
    const auto result = openStartupFiles(window.get(), {"textpinnacle"}, root.path());
    expect(result.opened.isEmpty() && result.failures.isEmpty() && tabs(window.get())->count() == 1,
           "no startup args preserve normal startup tab");
}

void testAbsoluteRelativeMultipleSpacesAndDeduplication() {
    QTemporaryDir root;
    const QString absolute = root.filePath("absolute.txt");
    const QString relative = root.filePath("relative name.txt");
    writeBytes(absolute, "absolute"); writeBytes(relative, "relative");
    auto window = fresh(root.filePath("session"));
    const auto result = openStartupFiles(window.get(),
        {"textpinnacle", absolute, "relative name.txt", absolute}, root.path());
    expect(result.failures.isEmpty() && result.opened.size() == 3,
           "absolute, relative, spaced and duplicate arguments are independently accepted");
    expect(tabs(window.get())->count() == 2 && tabText(window.get(), 0) == "absolute" &&
               tabText(window.get(), 1) == "relative",
           "startup orchestration replaces placeholder and deduplicates logical files");
    auto *manager = window->findChild<SessionManager *>();
    manager->flush();
    const auto recovered = manager->documents();
    QSet<QString> recoveredPaths;
    for (const auto &document : recovered)
        recoveredPaths.insert(document.filePath);
    expect(recovered.size() == 2 && recoveredPaths.contains(absolute) &&
               recoveredPaths.contains(relative),
           "startup files retain their paths as recoverable logical documents");
}

void testMissingAndMixedArgumentsContinueNonBlocking() {
    QTemporaryDir root; const QString good = root.filePath("good.txt"); writeBytes(good, "good");
    auto window = fresh(root.filePath("session"));
    const auto result = openStartupFiles(window.get(),
        {"textpinnacle", "--style=fusion", "missing.txt", good}, root.path());
    expect(result.opened.size() == 1 && result.failures.size() == 1 &&
               result.failures.first().path.endsWith("missing.txt"),
           "options are ignored and a missing file does not block a valid file");
    expect(tabs(window.get())->count() == 1 && tabText(window.get(), 0) == "good",
           "mixed valid and invalid startup input opens the valid logical document");
}

void testDoubleDashAllowsDashPrefixedFile() {
    QTemporaryDir root; writeBytes(root.filePath("-notes.txt"), "dash");
    auto window = fresh(root.filePath("session"));
    const auto result = openStartupFiles(window.get(),
        {"textpinnacle", "--", "-notes.txt"}, root.path());
    expect(result.opened.size() == 1 && result.failures.isEmpty() && tabText(window.get(), 0) == "dash",
           "double dash permits dash-prefixed filenames");
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    testNoArgsPreservesStartup();
    testAbsoluteRelativeMultipleSpacesAndDeduplication();
    testMissingAndMixedArgumentsContinueNonBlocking();
    testDoubleDashAllowsDashPrefixedFile();
    qunsetenv("NPP_SESSION_DIR");
    return failures == 0 ? 0 : 1;
}
