#include "editorutils.h"
#include "filebrowser.h"
#include "mainwindow.h"
#include "sessionmanager.h"
#include "ScintillaEditBase.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
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
    while (timer.elapsed() < milliseconds)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open fixture for writing");
    expect(file.write(bytes) == bytes.size(), "write fixture bytes");
}

QAction *actionWithText(QObject *root, QString text)
{
    for (QAction *action : root->findChildren<QAction *>()) {
        QString candidate = action->text();
        candidate.remove(QLatin1Char('&'));
        if (candidate == text && action->parent() == root)
            return action;
    }
    return nullptr;
}

void chooseMessageBox(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, [button] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                if (QAbstractButton *target = box->button(button))
                    target->click();
                else
                    box->done(button);
                return;
            }
        }
    });
}

void chooseSavePath(const QString &path)
{
    QTimer::singleShot(0, [path] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) {
                dialog->selectFile(path);
                static_cast<QDialog *>(dialog)->accept();
                return;
            }
        }
    });
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

void activateFile(QMainWindow *window, const QString &path)
{
    auto *browser = window->findChild<FileBrowser *>(QStringLiteral("FileBrowserDock"));
    browser->setRootPath(QFileInfo(path).absolutePath());
    auto *tree = browser->findChild<QTreeView *>(QStringLiteral("FileBrowserTree"));
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    tree->doubleClicked(waitForPath(model, path));
    processFor(80);
}

void testSessionRoundTripAndConflictDiagnostics()
{
    QTemporaryDir root;
    const QString original = root.filePath(QStringLiteral("named-λ.txt"));
    writeBytes(original, "original\n");
    const QString sessionDir = root.filePath(QStringLiteral("session"));

    SessionManager writer(nullptr, sessionDir, 20);
    writer.loadSession();
    writer.updateDocument({QStringLiteral("untitled-id"), {}, 7, true},
                          QStringLiteral("雪 untitled\n").toUtf8());
    writer.updateDocument({QStringLiteral("named-id"), original, 0, true},
                          QStringLiteral("dirty café\n").toUtf8());
    writer.setSessionLayout({QStringLiteral("untitled-id"), QStringLiteral("named-id")},
                            QStringLiteral("named-id"));
    writer.flush();

    expect(QFile(original).open(QIODevice::ReadOnly), "original remains readable after snapshot");
    QFile unchanged(original);
    expect(unchanged.open(QIODevice::ReadOnly), "open unchanged named original");
    expect(unchanged.readAll() == "original\n", "snapshot never writes named original");

    SessionManager reader(nullptr, sessionDir, 20);
    reader.loadSession();
    const auto docs = reader.documents();
    expect(docs.size() == 2, "round trip restores two documents");
    expect(docs.value(0).id == QStringLiteral("untitled-id") &&
               docs.value(1).id == QStringLiteral("named-id"),
           "round trip preserves tab order and stable identities");
    expect(reader.activeDocumentId() == QStringLiteral("named-id"),
           "round trip preserves active document identity");
    expect(reader.readRecoveryContent(docs.value(0)) == QStringLiteral("雪 untitled\n").toUtf8(),
           "untitled Unicode snapshot restores exactly");
    expect(reader.readRecoveryContent(docs.value(1)) == QStringLiteral("dirty café\n").toUtf8(),
           "dirty named Unicode snapshot restores exactly");
    expect(docs.value(0).dirty && docs.value(1).dirty, "dirty state persists");

    writeBytes(original, "external change\n");
    SessionManager conflicted(nullptr, sessionDir, 20);
    conflicted.loadSession();
    expect(conflicted.documents().value(1).recoveryState == RecoveryState::OriginalChanged,
           "external original change is surfaced");
    expect(conflicted.readRecoveryContent(conflicted.documents().value(1)) ==
               QStringLiteral("dirty café\n").toUtf8(),
           "external change does not replace recovered dirty content");
}

void testDirtySnapshotReactivationKeepsLatestBytes()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    const QString id = QStringLiteral("reactivated-id");
    const QByteArray latest = QStringLiteral("latest exact bytes 雪\0tail").toUtf8();

    SessionManager writer(nullptr, sessionDir, 20);
    writer.loadSession();
    writer.updateDocument({id, {}, 1, true}, "first dirty bytes");
    writer.flush();
    writer.updateDocument({id, {}, 1, false}, {});
    writer.updateDocument({id, {}, 1, true}, latest);
    writer.flush();

    SessionManager reader(nullptr, sessionDir, 20);
    reader.loadSession();
    expect(reader.documents().size() == 1 && reader.documents().first().dirty,
           "reactivated dirty document survives restart");
    expect(reader.documents().value(0).recoveryState == RecoveryState::Ready,
           "reactivated snapshot still exists after metadata commit cleanup");
    expect(reader.readRecoveryContent(reader.documents().value(0)) == latest,
           "reactivated snapshot restores latest exact bytes");
}

void testCheckpointFailuresReturnStatusAndCanRetry()
{
    QTemporaryDir root;

    const QString blockedDir = root.filePath(QStringLiteral("blocked"));
    QDir().mkpath(blockedDir);
    writeBytes(blockedDir + QStringLiteral("/session.json"),
               QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 999}}).toJson());
    SessionManager blocked(nullptr, blockedDir, 20);
    blocked.loadSession();
    expect(blocked.flush() == CheckpointStatus::WritesBlocked,
           "flush reports recovery writes blocked by unsupported metadata");

    const QString snapshotDir = root.filePath(QStringLiteral("snapshot-failure"));
    QDir().mkpath(snapshotDir);
    writeBytes(snapshotDir + QStringLiteral("/snapshots"), "blocks snapshot directory");
    SessionManager snapshotFailure(nullptr, snapshotDir, 20);
    snapshotFailure.loadSession();
    snapshotFailure.updateDocument({QStringLiteral("dirty"), {}, 1, true}, "must survive");
    expect(snapshotFailure.flush() == CheckpointStatus::SnapshotWriteFailed,
           "flush reports snapshot write failure");
    QFile::remove(snapshotDir + QStringLiteral("/snapshots"));
    QDir().mkpath(snapshotDir + QStringLiteral("/snapshots"));
    expect(snapshotFailure.flush() == CheckpointStatus::Durable,
           "failed snapshot remains pending and retry becomes durable");

    const QString metadataDir = root.filePath(QStringLiteral("metadata-failure"));
    SessionManager metadataFailure(nullptr, metadataDir, 20);
    metadataFailure.loadSession();
    metadataFailure.updateDocument({QStringLiteral("clean"), {}, 1, false}, {});
    QDir().mkpath(metadataDir + QStringLiteral("/session.json"));
    expect(metadataFailure.flush() == CheckpointStatus::MetadataWriteFailed,
           "flush reports metadata write failure");
    QDir(metadataDir + QStringLiteral("/session.json")).removeRecursively();
    expect(metadataFailure.flush() == CheckpointStatus::Durable,
           "failed metadata remains dirty and retry becomes durable");
}

void testFailedShutdownStaysOpenAndOffersRetry()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    QDir().mkpath(sessionDir);
    writeBytes(sessionDir + QStringLiteral("/snapshots"), "blocks snapshot directory");
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());

    QMainWindow *window = createMainWindow();
    window->show();
    auto *editor = window->findChild<QTabWidget *>()->currentWidget()
                       ->findChild<ScintillaEditBase *>();
    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("not durable yet"));
    expect(window->findChild<SessionManager *>()->flush() ==
               CheckpointStatus::SnapshotWriteFailed,
           "window fixture triggers snapshot persistence failure");
    expect(window->statusBar()->currentMessage().contains(
               QStringLiteral("Could not write recovery snapshot")),
           "runtime checkpoint failure is visible in the window");

    chooseMessageBox(QMessageBox::Cancel);
    expect(!window->close() && window->isVisible(),
           "cancelling failed shutdown keeps unsaved application open");

    QFile::remove(sessionDir + QStringLiteral("/snapshots"));
    QDir().mkpath(sessionDir + QStringLiteral("/snapshots"));
    chooseMessageBox(QMessageBox::Retry);
    expect(window->close(), "retry after persistence repair permits shutdown");
    delete window;
    qunsetenv("NPP_SESSION_DIR");
}

void testMalformedMetadataMissingBackupAndOrphanPreservation()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    QDir().mkpath(sessionDir + QStringLiteral("/snapshots"));
    const QString orphan = sessionDir + QStringLiteral("/snapshots/orphan.snapshot");
    writeBytes(orphan, "recoverable orphan");
    writeBytes(sessionDir + QStringLiteral("/session.json"), "{broken json");

    SessionManager malformed(nullptr, sessionDir, 20);
    malformed.loadSession();
    expect(!malformed.diagnostics().isEmpty(), "malformed metadata reports diagnostics");
    expect(malformed.writesBlocked(), "malformed metadata blocks destructive recovery writes");
    expect(QFileInfo::exists(orphan), "malformed metadata never deletes orphan snapshots");
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());
    QMainWindow *malformedWindow = createMainWindow();
    malformedWindow->show();
    processFor(20);
    chooseMessageBox(QMessageBox::Discard);
    malformedWindow->close();
    delete malformedWindow;
    QFile retainedMalformed(sessionDir + QStringLiteral("/session.json"));
    expect(retainedMalformed.open(QIODevice::ReadOnly) && retainedMalformed.readAll() == "{broken json",
           "main-window startup and shutdown preserve malformed metadata");
    retainedMalformed.close();
    qunsetenv("NPP_SESSION_DIR");

    const QString missingSnapshot = QStringLiteral("snapshots/%1.snapshot").arg(
        QString::fromLatin1(QCryptographicHash::hash(QByteArrayLiteral("lost"),
                                                     QCryptographicHash::Sha256).toHex()));
    QJsonObject doc{{QStringLiteral("id"), QStringLiteral("lost")},
                    {QStringLiteral("dirty"), true},
                    {QStringLiteral("untitledNumber"), 1},
                    {QStringLiteral("snapshot"), missingSnapshot}};
    QJsonObject metadata{{QStringLiteral("schemaVersion"), 2},
                         {QStringLiteral("activeDocumentId"), QStringLiteral("lost")},
                         {QStringLiteral("documents"), QJsonArray{doc}}};
    writeBytes(sessionDir + QStringLiteral("/session.json"),
               QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    SessionManager missing(nullptr, sessionDir, 20);
    missing.loadSession();
    expect(missing.documents().size() == 1, "missing backup retains document metadata");
    expect(missing.documents().value(0).recoveryState == RecoveryState::SnapshotMissing,
           "missing backup is explicitly surfaced");
    expect(!missing.diagnostics().isEmpty(), "missing backup reports diagnostics");
    expect(!missing.writesBlocked(), "valid missing backup remains writable for future edits");
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());
    QMainWindow *missingWindow = createMainWindow();
    missingWindow->close();
    delete missingWindow;
    expect(!QFileInfo::exists(QDir(sessionDir).filePath(missingSnapshot)),
           "shutdown does not replace a missing snapshot with empty data");
    SessionManager stillMissing(nullptr, sessionDir, 20);
    stillMissing.loadSession();
    expect(stillMissing.documents().value(0).recoveryState == RecoveryState::SnapshotMissing,
           "missing snapshot remains diagnosable after main-window lifecycle");
    qunsetenv("NPP_SESSION_DIR");
}

void testUnmanagedSnapshotPathCannotDeleteFiles()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    QDir().mkpath(sessionDir);
    const QString victim = root.filePath(QStringLiteral("victim.txt"));
    writeBytes(victim, "do not delete");
    QJsonObject document{{QStringLiteral("id"), QStringLiteral("hostile")},
                         {QStringLiteral("dirty"), true},
                         {QStringLiteral("untitledNumber"), 1},
                         {QStringLiteral("snapshot"), victim}};
    QJsonObject metadata{{QStringLiteral("schemaVersion"), 2},
                         {QStringLiteral("documents"), QJsonArray{document}}};
    writeBytes(sessionDir + QStringLiteral("/session.json"), QJsonDocument(metadata).toJson());
    SessionManager manager(nullptr, sessionDir, 20);
    manager.loadSession();
    manager.removeDocument(QStringLiteral("hostile"));
    manager.flush();
    expect(manager.writesBlocked(), "unmanaged snapshot path blocks recovery writes");
    expect(QFileInfo::exists(victim), "unmanaged snapshot path cannot delete arbitrary files");
}

void testLegacyMigrationKeepsSourceAndDeduplicates()
{
    QTemporaryDir root;
    const QString canonical = root.filePath(QStringLiteral("canonical"));
    const QString legacy = root.filePath(QStringLiteral("legacy"));
    QDir().mkpath(legacy);
    writeBytes(legacy + QStringLiteral("/new_3.backup"), QStringLiteral("legacy 雪").toUtf8());
    QJsonObject oldTab{{QStringLiteral("tabNumber"), 3},
                       {QStringLiteral("backupPath"), legacy + QStringLiteral("/new_3.backup")}};
    QJsonObject old{{QStringLiteral("activeTab"), 3},
                    {QStringLiteral("untitledTabs"), QJsonArray{oldTab, oldTab}}};
    writeBytes(legacy + QStringLiteral("/session.json"), QJsonDocument(old).toJson());

    SessionManager manager(nullptr, canonical, 20, {legacy});
    manager.loadSession();
    expect(manager.documents().size() == 1, "legacy migration deduplicates duplicate tabs");
    expect(manager.readRecoveryContent(manager.documents().first()) ==
               QStringLiteral("legacy 雪").toUtf8(),
           "legacy migration imports exact bytes");
    expect(QFileInfo::exists(legacy + QStringLiteral("/session.json")) &&
               QFileInfo::exists(legacy + QStringLiteral("/new_3.backup")),
           "legacy migration preserves source metadata and backup");
    manager.flush();
    SessionManager again(nullptr, canonical, 20, {legacy});
    again.loadSession();
    expect(again.documents().size() == 1, "legacy migration remains deduplicated on restart");
}

void testAtomicOrdinarySaveFailurePreservesOriginal()
{
    QTemporaryDir root;
    const QString path = root.filePath(QStringLiteral("original.txt"));
    writeBytes(path, "before");
    ScintillaEditBase editor;
    editor.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("after"));
    QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    QString error;
    const bool saved = EditorUtils::writeToFile(&editor, path, &error);
    QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner);
    QFile original(path);
    expect(original.open(QIODevice::ReadOnly), "open original after failed save");
    expect(!saved, "ordinary save reports atomic replacement failure");
    expect(original.readAll() == "before", "failed ordinary save preserves original bytes");
    expect(!error.isEmpty(), "failed ordinary save surfaces error text");
}

void testMainWindowRestartPreservesOrderActiveAndDirty()
{
    QTemporaryDir root;
    qputenv("NPP_SESSION_DIR", root.filePath(QStringLiteral("session")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("npp-recovery-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("isolated"));

    QMainWindow *first = createMainWindow();
    auto *tabs = first->findChild<QTabWidget *>();
    auto *firstEditor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    firstEditor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(QStringLiteral("first 雪").toUtf8().constData()));
    actionWithText(first, QStringLiteral("New"))->trigger();
    auto *secondEditor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    secondEditor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("second"));
    tabs->setCurrentIndex(0);
    processFor(80);
    first->close();
    delete first;

    QMainWindow *second = createMainWindow();
    tabs = second->findChild<QTabWidget *>();
    expect(tabs->count() == 2, "normal exit restores all unsaved tabs without prompts");
    expect(tabs->currentIndex() == 0, "normal exit restores active tab");
    expect(EditorUtils::text(tabs->widget(0)->findChild<ScintillaEditBase *>()) ==
               QStringLiteral("first 雪").toUtf8() &&
               EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) == "second",
           "main-window restart restores exact content and order");
    expect(tabs->tabText(0).endsWith(QLatin1Char('*')) &&
               tabs->tabText(1).endsWith(QLatin1Char('*')),
           "main-window restart restores dirty state");
    second->close();
    delete second;
    qunsetenv("NPP_SESSION_DIR");
}

void testCancelAndDiscardRecoveryLifecycle()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());
    QMainWindow *window = createMainWindow();
    auto *tabs = window->findChild<QTabWidget *>();
    auto *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("keep snapshot"));
    processFor(40);
    const int count = tabs->count();
    chooseMessageBox(QMessageBox::Cancel);
    QMetaObject::invokeMethod(window, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0));
    expect(tabs->count() == count && EditorUtils::text(editor) == "keep snapshot",
           "cancel preserves content and tab");
    window->findChild<SessionManager *>()->flush();
    expect(window->findChild<SessionManager *>()->documents().size() == 1,
           "cancel preserves recovery metadata");

    chooseMessageBox(QMessageBox::Discard);
    QMetaObject::invokeMethod(window, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0));
    window->findChild<SessionManager *>()->flush();
    expect(window->findChild<SessionManager *>()->documents().size() == 1 &&
               window->findChild<SessionManager *>()->documents().first().dirty == false,
           "discard removes closed recovery and replacement blank is clean");
    window->close();
    delete window;
    qunsetenv("NPP_SESSION_DIR");
}

void testDirtyNamedMainWindowRestart()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    const QString originalPath = root.filePath(QStringLiteral("original.txt"));
    writeBytes(originalPath, "original bytes");
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());

    QMainWindow *first = createMainWindow();
    activateFile(first, originalPath);
    auto *tabs = first->findChild<QTabWidget *>();
    auto *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    editor->send(SCI_SETTEXT, 0,
                 reinterpret_cast<sptr_t>(QStringLiteral("dirty named café 雪").toUtf8().constData()));
    first->close();
    delete first;

    QFile original(originalPath);
    expect(original.open(QIODevice::ReadOnly) && original.readAll() == "original bytes",
           "dirty named checkpoint never writes the original");
    QMainWindow *second = createMainWindow();
    tabs = second->findChild<QTabWidget *>();
    expect(tabs->count() == 2 && tabs->currentIndex() == 1,
           "dirty named restart preserves order and active selection");
    expect(EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) ==
               QStringLiteral("dirty named café 雪").toUtf8() &&
               tabs->tabText(1).endsWith(QLatin1Char('*')),
           "dirty named restart restores exact Unicode snapshot and dirty state");
    second->close();
    delete second;
    qunsetenv("NPP_SESSION_DIR");
}

void testDuplicateNamedPathsRestoreByStableIdentity()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    const QString sharedPath = root.filePath(QStringLiteral("shared.txt"));
    writeBytes(sharedPath, "original");
    {
        SessionManager writer(nullptr, sessionDir, 20);
        writer.loadSession();
        writer.updateDocument({QStringLiteral("first-id"), sharedPath, 0, true}, "first recovered");
        writer.updateDocument({QStringLiteral("second-id"), sharedPath, 0, true}, "second recovered");
        writer.setSessionLayout({QStringLiteral("first-id"), QStringLiteral("second-id")},
                                QStringLiteral("second-id"));
        writer.flush();
    }
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());
    QMainWindow *window = createMainWindow();
    auto *tabs = window->findChild<QTabWidget *>();
    expect(tabs->count() == 2 && tabs->currentIndex() == 1,
           "duplicate named paths restore as distinct stable identities");
    expect(EditorUtils::text(tabs->widget(0)->findChild<ScintillaEditBase *>()) == "first recovered" &&
               EditorUtils::text(tabs->widget(1)->findChild<ScintillaEditBase *>()) == "second recovered",
           "duplicate named paths retain each recovered snapshot");
    window->close();
    delete window;
    qunsetenv("NPP_SESSION_DIR");
}

void testSaveSaveAsAndFailedSaveLifecycle()
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("session"));
    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());

    QMainWindow *window = createMainWindow();
    auto *tabs = window->findChild<QTabWidget *>();
    auto *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(QStringLiteral("save-as 雪").toUtf8().constData()));
    const QString saveAsPath = root.filePath(QStringLiteral("saved.txt"));
    chooseSavePath(saveAsPath);
    actionWithText(window, QStringLiteral("Save As"))->trigger();
    QFile saved(saveAsPath);
    expect(saved.open(QIODevice::ReadOnly) && saved.readAll() == QStringLiteral("save-as 雪").toUtf8(),
           "Save As writes exact Unicode bytes");
    auto *manager = window->findChild<SessionManager *>();
    manager->flush();
    expect(manager->documents().size() == 1 && !manager->documents().first().dirty &&
               manager->documents().first().filePath == saveAsPath &&
               manager->documents().first().snapshotPath.isEmpty(),
           "Save As updates identity to clean named document and removes recovery snapshot");

    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("ordinary save"));
    actionWithText(window, QStringLiteral("Save"))->trigger();
    saved.close();
    expect(saved.open(QIODevice::ReadOnly) && saved.readAll() == "ordinary save" &&
               !tabs->tabText(tabs->currentIndex()).endsWith(QLatin1Char('*')),
           "Save updates original and clears dirty state");

    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("must remain dirty"));
    QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    chooseMessageBox(QMessageBox::Ok);
    actionWithText(window, QStringLiteral("Save"))->trigger();
    QFile::setPermissions(root.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner);
    saved.close();
    expect(saved.open(QIODevice::ReadOnly) && saved.readAll() == "ordinary save",
           "failed Save preserves original bytes");
    expect(tabs->tabText(tabs->currentIndex()).endsWith(QLatin1Char('*')),
           "failed Save leaves editor dirty");

    window->close();
    delete window;
    qunsetenv("NPP_SESSION_DIR");
}

void testCrashCheckpointRecovery(const QString &executable)
{
    QTemporaryDir root;
    const QString sessionDir = root.filePath(QStringLiteral("crash-session"));
    QProcess helper;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    helper.setProcessEnvironment(environment);
    helper.start(executable, {QStringLiteral("--checkpoint-helper"), sessionDir});
    expect(helper.waitForStarted(3000), "crash helper starts");
    expect(helper.waitForReadyRead(5000) && helper.readAllStandardOutput().contains("CHECKPOINTED"),
           "crash helper reaches debounced checkpoint");
    helper.kill();
    helper.waitForFinished(3000);

    qputenv("NPP_SESSION_DIR", sessionDir.toUtf8());
    QMainWindow *window = createMainWindow();
    auto *tabs = window->findChild<QTabWidget *>();
    expect(tabs->count() == 1 &&
               EditorUtils::text(tabs->currentWidget()->findChild<ScintillaEditBase *>()) ==
                   QStringLiteral("crash 雪").toUtf8() &&
               tabs->tabText(0).endsWith(QLatin1Char('*')),
           "terminated process restores checkpointed dirty Unicode buffer");
    window->close();
    delete window;
    qunsetenv("NPP_SESSION_DIR");
}
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("npp-recovery-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("isolated"));
    QSettings().clear();
    if (app.arguments().contains(QStringLiteral("--failed-shutdown-test"))) {
        testFailedShutdownStaysOpenAndOffersRetry();
        return failures == 0 ? 0 : 1;
    }
    if (app.arguments().size() == 3 && app.arguments().at(1) == QStringLiteral("--checkpoint-helper")) {
        qputenv("NPP_SESSION_DIR", app.arguments().at(2).toUtf8());
        QMainWindow *window = createMainWindow();
        auto *tabs = window->findChild<QTabWidget *>();
        auto *editor = tabs->currentWidget()->findChild<ScintillaEditBase *>();
        editor->send(SCI_SETTEXT, 0,
                     reinterpret_cast<sptr_t>(QStringLiteral("crash 雪").toUtf8().constData()));
        QTimer::singleShot(SessionManager::DefaultCheckpointIntervalMs + 500, [] {
            std::puts("CHECKPOINTED");
            std::fflush(stdout);
        });
        return app.exec();
    }
    testSessionRoundTripAndConflictDiagnostics();
    testDirtySnapshotReactivationKeepsLatestBytes();
    testCheckpointFailuresReturnStatusAndCanRetry();
    testFailedShutdownStaysOpenAndOffersRetry();
    testMalformedMetadataMissingBackupAndOrphanPreservation();
    testUnmanagedSnapshotPathCannotDeleteFiles();
    testLegacyMigrationKeepsSourceAndDeduplicates();
    testAtomicOrdinarySaveFailurePreservesOriginal();
    testMainWindowRestartPreservesOrderActiveAndDirty();
    testCancelAndDiscardRecoveryLifecycle();
    testDirtyNamedMainWindowRestart();
    testDuplicateNamedPathsRestoreByStableIdentity();
    testSaveSaveAsAndFailedSaveLifecycle();
    testCrashCheckpointRecovery(app.applicationFilePath());
    if (failures == 0)
        std::puts("All Linux recovery tests passed");
    return failures == 0 ? 0 : 1;
}
