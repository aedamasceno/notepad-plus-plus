#include "documentformat.h"
#include "editorutils.h"
#include "mainwindow.h"
#include "sessionmanager.h"
#include "ScintillaEditBase.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>

namespace {
int failures = 0;
void expect(bool value, const char *message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path); expect(file.open(QIODevice::WriteOnly), "test fixture opens for write");
    expect(file.write(bytes) == bytes.size(), "test fixture writes all bytes");
}
QByteArray readBytes(const QString &path)
{
    QFile file(path); expect(file.open(QIODevice::ReadOnly), "test output opens for read");
    return file.readAll();
}
QAction *action(QObject *root, const char *name)
{
    return root->findChild<QAction *>(QString::fromLatin1(name));
}
ScintillaEditBase *activeEditor(QMainWindow *window)
{
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    for (QTabWidget *tabs : {secondary, primary})
        if (tabs && tabs->hasFocus() && tabs->currentWidget())
            return tabs->currentWidget()->findChild<ScintillaEditBase *>();
    return primary && primary->currentWidget()
        ? primary->currentWidget()->findChild<ScintillaEditBase *>() : nullptr;
}

void testDetectionAndRoundTrips()
{
    using namespace DocumentFormat;
    const QByteArray unicode = QStringLiteral("snow 雪 €\r\n").toUtf8();
    const auto ascii = decode("plain\ntext");
    expect(ascii.success && ascii.encoding == TextEncoding::Utf8, "ASCII is canonical UTF-8");
    const auto utf8 = decode(unicode);
    expect(utf8.success && utf8.encoding == TextEncoding::Utf8 && utf8.utf8 == unicode,
           "strict multibyte UTF-8 is detected");
    const auto bom = decode(QByteArray::fromHex("efbbbf") + unicode);
    expect(bom.success && bom.encoding == TextEncoding::Utf8Bom && bom.utf8 == unicode,
           "UTF-8 BOM is stripped from editable text");
    const auto cp1252 = decode(QByteArray("price \x80", 7));
    expect(cp1252.success && cp1252.encoding == TextEncoding::Windows1252 &&
               cp1252.utf8 == QStringLiteral("price €").toUtf8(),
           "invalid UTF-8 deterministically falls back to Windows-1252");
    expect(!decode(QByteArray("a\0b", 3)).success, "NUL-bearing binary input is rejected");
    expect(!isValidUtf8Text(QByteArray("bad\xff", 4)) &&
               !isValidUtf8Text(QByteArray("a\0b", 3)),
           "canonical recovery UTF-8 rejects malformed bytes and NUL");

    QByteArray bytes;
    for (TextEncoding encoding : {TextEncoding::Utf8, TextEncoding::Utf8Bom,
                                  TextEncoding::Utf16Le, TextEncoding::Utf16Be}) {
        expect(encode(unicode, encoding, &bytes), "Unicode encoding succeeds");
        const auto decoded = decode(bytes);
        expect(decoded.success && decoded.encoding == encoding && decoded.utf8 == unicode,
               "Unicode encoding round trips with metadata");
    }
    expect(encode(QStringLiteral("café €\n").toUtf8(), TextEncoding::Windows1252, &bytes) &&
               decode(bytes).utf8 == QStringLiteral("café €\n").toUtf8(),
           "Windows-1252 representable text round trips");
    expect(!encode(QStringLiteral("snow 雪").toUtf8(), TextEncoding::Windows1252, &bytes),
           "Windows-1252 rejects unrepresentable Unicode");
    expect(!decode(QByteArray::fromHex("fffe00d8")).success &&
               !decode(QByteArray::fromHex("feffd800")).success,
           "malformed UTF-16 surrogates are rejected for both endiannesses");

    expect(encode(bom.utf8, TextEncoding::Utf8Bom, &bytes) &&
               bytes.startsWith(QByteArray::fromHex("efbbbf")) &&
               !bytes.mid(3).startsWith(QByteArray::fromHex("efbbbf")),
           "BOM restoration never doubles the BOM");
    const QByteArray intentional = QByteArray::fromHex("efbbbfefbbbf") + QByteArray("text");
    const auto intentionalDecoded = decode(intentional);
    expect(intentionalDecoded.success &&
               encode(intentionalDecoded.utf8, intentionalDecoded.encoding, &bytes) &&
               bytes == intentional,
           "a second BOM sequence is preserved only as intentional U+FEFF content");
}

void testAtomicFailureAndEols()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    const QString path = temporary.filePath("original.txt");
    writeBytes(path, "untouched");
    QString error;
    expect(!saveAtomic(path, QStringLiteral("雪").toUtf8(), TextEncoding::Windows1252, &error) &&
               readBytes(path) == "untouched" && !error.isEmpty(),
           "lossy CP1252 save fails before touching the original");

    const auto crlf = scanEols("a\r\nb\r\n");
    const auto lf = scanEols("a\nb\n");
    const auto cr = scanEols("a\rb\r");
    const auto mixed = scanEols("a\r\nb\nc\r");
    const auto tie = scanEols("a\nb\r\n");
    const auto leadersTie = scanEols("a\nb\rc\r\nd\re\r\n");
    expect(crlf.kind == EolKind::CrLf && lf.kind == EolKind::Lf && cr.kind == EolKind::Cr,
           "single EOL conventions are detected");
    expect(mixed.kind == EolKind::Mixed && mixed.insertion == EolKind::CrLf,
           "mixed EOL reports mixed and uses the predominant convention");
    expect(tie.kind == EolKind::Mixed && tie.insertion == EolKind::Lf,
           "predominant ties deterministically use the first encountered EOL");
    expect(leadersTie.insertion == EolKind::Cr,
           "ties use the earliest occurrence among the predominant conventions");
    expect(scanEols("one line").kind == EolKind::None &&
               scanEols("one line").insertion == EolKind::Lf,
           "no-EOL documents default insertion to LF");
}

void testWindowMenusSaveAndClones()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    qputenv("NPP_SESSION_DIR", temporary.filePath("session").toUtf8());
    const QString source = temporary.filePath("mixed.txt");
    writeBytes(source, QByteArray::fromHex("efbbbf") + QByteArray("a\r\nb\nc\r\nd"));
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), source, &error), "window opens encoded file through real orchestration");
    auto *editor = activeEditor(window.get());
    expect(editor && editor->send(SCI_GETCODEPAGE) == SC_CP_UTF8,
           "Scintilla always uses the UTF-8 code page");
    expect(EditorUtils::text(editor) == "a\r\nb\nc\r\nd", "editable buffer contains UTF-8 without BOM");
    expect(editor->send(SCI_GETEOLMODE) == SC_EOL_CRLF,
           "mixed document insertion mode uses predominant CRLF");
    expect(window->statusBar()->currentMessage().contains("UTF-8 BOM") &&
               window->statusBar()->currentMessage().contains("Mixed EOL"),
           "status shows true disk encoding and scanned EOL state");

    const char *encodingActions[] = {"encodingUtf8Action", "encodingUtf8BomAction",
        "encodingUtf16LeAction", "encodingUtf16BeAction", "encodingWindows1252Action"};
    for (const char *name : encodingActions)
        expect(action(window.get(), name) && action(window.get(), name)->isCheckable(),
               "every Encoding menu action exists and is checkable");
    action(window.get(), "encodingUtf16BeAction")->trigger();
    expect(EditorUtils::text(editor) == "a\r\nb\nc\r\nd" &&
               action(window.get(), "encodingUtf16BeAction")->isChecked(),
           "encoding conversion preserves Unicode and updates check state");

    action(window.get(), "cloneToOtherViewAction")->trigger();
    QApplication::processEvents();
    auto *secondaryTabs = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *cloneEditor = secondaryTabs->currentWidget()->findChild<ScintillaEditBase *>();
    expect(action(window.get(), "encodingUtf16BeAction")->isChecked(),
           "clones share one encoding state");
    expect(saveCurrentFileInMainWindow(window.get(), &error) &&
               readBytes(source).startsWith(QByteArray::fromHex("feff")),
           "Save writes the selected representation atomically");
    const QString copy = temporary.filePath("copy.txt");
    expect(saveCurrentFileAsInMainWindow(window.get(), copy, &error) &&
               readBytes(copy).startsWith(QByteArray::fromHex("feff")),
           "Save As retains current encoding");

    action(window.get(), "eolLfAction")->trigger();
    expect(EditorUtils::text(editor) == "a\nb\nc\nd" && editor->send(SCI_GETEOLMODE) == SC_EOL_LF &&
               cloneEditor->send(SCI_GETEOLMODE) == SC_EOL_LF &&
               window->statusBar()->currentMessage().contains("Unix (LF)"),
           "EOL conversion updates content, status, and every clone insertion mode");
    expect(saveCurrentFileInMainWindow(window.get(), &error), "converted document saves");
}

void testFreshLfAndCp1252FailureStaysDirty()
{
    QTemporaryDir temporary;
    qputenv("NPP_SESSION_DIR", temporary.filePath("session").toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *editor = activeEditor(window.get());
    expect(editor && editor->send(SCI_GETEOLMODE) == SC_EOL_LF &&
               window->statusBar()->currentMessage().contains("No EOL"),
           "new Linux untitled documents insert LF and report no EOL");
    action(window.get(), "encodingWindows1252Action")->trigger();
    const QByteArray snow = QStringLiteral("雪").toUtf8();
    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(snow.constData()));
    const QString destination = temporary.filePath("blocked.txt");
    writeBytes(destination, "old");
    QString error;
    expect(!saveCurrentFileAsInMainWindow(window.get(), destination, &error) &&
               readBytes(destination) == "old" && window->findChild<QTabWidget *>("primaryTabWidget")->tabText(0).endsWith('*'),
           "unrepresentable CP1252 Save As preserves destination and dirty state");
}

void testCleanRecoveryUsesCurrentDiskFormat()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    const QString session = temporary.filePath("session");
    const QString path = temporary.filePath("external-format.txt");
    QByteArray staleBytes;
    expect(encode("old\rold\r", TextEncoding::Utf16Be, &staleBytes),
           "stale clean-format fixture encodes UTF-16 BE");
    writeBytes(path, staleBytes);
    SessionManager writer(nullptr, session, 1);
    writer.loadSession();
    writer.updateDocument({"clean-format", path, 0, false, TextEncoding::Utf16Be,
                           EolKind::Cr, EolKind::Cr}, {});
    writer.setSessionLayout({"clean-format"}, "clean-format");
    writer.setDualViewLayout({"clean-format"}, {}, "primary", Qt::Horizontal, {});
    expect(writer.flush() == CheckpointStatus::Durable,
           "stale clean-format metadata fixture is durable");

    const QByteArray currentDisk("current\nutf8\n");
    writeBytes(path, currentDisk);
    qputenv("NPP_SESSION_DIR", session.toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *editor = activeEditor(window.get());
    expect(editor && EditorUtils::text(editor) == currentDisk,
           "clean session restores current externally changed disk bytes");
    expect(window->statusBar()->currentMessage().contains("UTF-8") &&
               window->statusBar()->currentMessage().contains("Unix (LF)"),
           "clean session reports encoding and EOL decoded from current disk");
    QString error;
    expect(saveCurrentFileInMainWindow(window.get(), &error) && readBytes(path) == currentDisk,
           "ordinary save of clean restored file does not convert current disk format");
    expect(window->findChild<SessionManager *>()->flush() == CheckpointStatus::Durable,
           "authoritative clean disk format checkpoints safely");
    const QJsonObject stored = QJsonDocument::fromJson(readBytes(session + "/session.json"))
                                   .object().value("documents").toArray().first().toObject();
    expect(stored.value("encoding") == "utf8" && stored.value("eol") == "lf" &&
               stored.value("insertionEol") == "lf",
           "checkpoint replaces stale clean format metadata with current disk format");
    window.reset();
    qunsetenv("NPP_SESSION_DIR");
}

void testRecoveryMetadataAndSchema3Migration()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    const QString session = temporary.filePath("recovery");
    SessionManager manager(nullptr, session, 1);
    manager.loadSession();
    DocumentCheckpoint checkpoint{"doc", temporary.filePath("utf16.txt"), 0, true,
                                  TextEncoding::Utf16Le, EolKind::Mixed, EolKind::Lf};
    manager.updateDocument(checkpoint, QStringLiteral("a\r\n雪\n").toUtf8());
    manager.setSessionLayout({"doc"}, "doc");
    manager.setDualViewLayout({"doc"}, {}, "primary", Qt::Horizontal, {});
    expect(manager.flush() == CheckpointStatus::Durable, "encoding-aware recovery checkpoint is durable");
    SessionManager loaded(nullptr, session, 1); loaded.loadSession();
    expect(loaded.documents().size() == 1 && loaded.documents().first().encoding == TextEncoding::Utf16Le &&
               loaded.documents().first().eol == EolKind::Mixed &&
               loaded.documents().first().insertionEol == EolKind::Lf,
           "schema 4 retains encoding and EOL metadata once per logical document");

    qputenv("NPP_SESSION_DIR", session.toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(saveCurrentFileInMainWindow(window.get(), &error) &&
               readBytes(temporary.filePath("utf16.txt")).startsWith(QByteArray::fromHex("fffe")),
           "recovered UTF-16 logical document saves back as UTF-16");

    const QString oldSession = temporary.filePath("schema3");
    QDir().mkpath(oldSession);
    const QJsonObject document{{"id", "old"}, {"filePath", temporary.filePath("old.txt")},
                               {"untitledNumber", 0}, {"dirty", false}, {"snapshot", ""},
                               {"originalExisted", false}, {"originalSize", -1},
                               {"originalMtimeMs", -1}, {"originalSha256", ""}};
    writeBytes(temporary.filePath("old.txt"), "old\r\n");
    const QJsonObject dual{{"primary", QJsonArray{"old"}}, {"secondary", QJsonArray{}},
                           {"activePane", "primary"}, {"orientation", "horizontal"},
                           {"sizes", QJsonArray{}}};
    writeBytes(oldSession + "/session.json", QJsonDocument(QJsonObject{
        {"schemaVersion", 3}, {"activeDocumentId", "old"},
        {"documents", QJsonArray{document}}, {"dualView", dual}}).toJson());
    SessionManager migrated(nullptr, oldSession, 1); migrated.loadSession();
    expect(!migrated.writesBlocked() && migrated.documents().first().encoding == TextEncoding::Utf8,
           "valid schema 3 migrates with safe UTF-8 metadata defaults");
    expect(migrated.flush() == CheckpointStatus::Durable &&
               QJsonDocument::fromJson(readBytes(oldSession + "/session.json")).object().value("schemaVersion").toInt() == SessionManager::SchemaVersion,
           "valid schema 3 migrates automatically after conservative loading");

    const QString dirtySession = temporary.filePath("schema3-dirty-utf16");
    QDir().mkpath(dirtySession + "/snapshots");
    const QString dirtyId = "legacy-utf16";
    const QString legacySnapshot = QStringLiteral("snapshots/%1.snapshot").arg(QString::fromLatin1(
        QCryptographicHash::hash(dirtyId.toUtf8(), QCryptographicHash::Sha256).toHex()));
    QByteArray legacyUtf16;
    expect(encode(QStringLiteral("legacy 雪\r\n").toUtf8(), TextEncoding::Utf16Le, &legacyUtf16),
           "legacy UTF-16 fixture encodes");
    writeBytes(dirtySession + "/" + legacySnapshot, legacyUtf16);
    QJsonObject dirtyDocument{{"id", dirtyId}, {"filePath", temporary.filePath("legacy-target.txt")},
                              {"untitledNumber", 0}, {"dirty", true}, {"snapshot", legacySnapshot},
                              {"originalExisted", false}, {"originalSize", -1},
                              {"originalMtimeMs", -1}, {"originalSha256", ""}};
    QJsonObject dirtyDual{{"primary", QJsonArray{dirtyId}}, {"secondary", QJsonArray{}},
                          {"activePane", "primary"}, {"orientation", "horizontal"},
                          {"sizes", QJsonArray{}}};
    writeBytes(dirtySession + "/session.json", QJsonDocument(QJsonObject{
        {"schemaVersion", 3}, {"activeDocumentId", dirtyId},
        {"documents", QJsonArray{dirtyDocument}}, {"dualView", dirtyDual}}).toJson());
    SessionManager dirtyMigration(nullptr, dirtySession, 1);
    dirtyMigration.loadSession();
    expect(dirtyMigration.flush() == CheckpointStatus::Durable,
           "dirty schema-3 UTF-16 migration writes canonical snapshot before schema 4 metadata");
    SessionManager afterMigration(nullptr, dirtySession, 1);
    afterMigration.loadSession();
    const auto migratedContent = afterMigration.readRecoveryContent(afterMigration.documents().first());
    expect(!afterMigration.writesBlocked() && migratedContent.success &&
               migratedContent.content == QStringLiteral("legacy 雪\r\n").toUtf8(),
           "migrated dirty UTF-16 snapshot survives a fresh schema-4 reload as canonical UTF-8");

    const QString missingSession = temporary.filePath("schema3-missing");
    QDir().mkpath(missingSession);
    dirtyDocument.insert("snapshot", legacySnapshot);
    const QByteArray missingMetadata = QJsonDocument(QJsonObject{
        {"schemaVersion", 3}, {"activeDocumentId", dirtyId},
        {"documents", QJsonArray{dirtyDocument}}, {"dualView", dirtyDual}}).toJson();
    writeBytes(missingSession + "/session.json", missingMetadata);
    SessionManager missingMigration(nullptr, missingSession, 1);
    missingMigration.loadSession();
    expect(missingMigration.writesBlocked() &&
               missingMigration.flush() == CheckpointStatus::WritesBlocked &&
               readBytes(missingSession + "/session.json") == missingMetadata,
           "schema-3 migration with an unreadable dirty snapshot preserves and blocks metadata");
}

void testLegacySnapshotCompatibilityAndIntegrity()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    const QString schema2 = temporary.filePath("schema2-legacy");
    QDir().mkpath(schema2 + "/snapshots");
    const QString id = QStringLiteral("schema2-utf16");
    const QString target = temporary.filePath("schema2-target.txt");
    const QString fixedSnapshot = QStringLiteral("snapshots/%1.snapshot").arg(QString::fromLatin1(
        QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()));
    QByteArray utf16;
    expect(encode(QStringLiteral("legacy 雪\rlegacy\r").toUtf8(), TextEncoding::Utf16Le, &utf16),
           "schema-2 legacy snapshot fixture encodes UTF-16");
    writeBytes(schema2 + "/" + fixedSnapshot, utf16);
    const QJsonObject legacyDocument{{"id", id}, {"filePath", target}, {"untitledNumber", 0},
                                     {"dirty", true}, {"snapshot", fixedSnapshot},
                                     {"originalExisted", false}, {"originalSize", -1},
                                     {"originalMtimeMs", -1}, {"originalSha256", ""}};
    writeBytes(schema2 + "/session.json", QJsonDocument(QJsonObject{
        {"schemaVersion", 2}, {"activeDocumentId", id},
        {"documents", QJsonArray{legacyDocument}}}).toJson());

    SessionManager legacyReader(nullptr, schema2, 1);
    legacyReader.loadSession();
    const auto legacyDocuments = legacyReader.documents();
    expect(!legacyReader.writesBlocked() && legacyDocuments.size() == 1 &&
               !legacyDocuments.first().legacyEncodedSnapshot &&
               legacyReader.readRecoveryContent(legacyDocuments.first()).content ==
                   QStringLiteral("legacy 雪\rlegacy\r").toUtf8(),
           "schema-2 fixed-name dirty snapshots canonicalize in memory before migration");
    legacyReader.setSessionLayout({id}, id);
    expect(legacyReader.flush() == CheckpointStatus::Durable,
           "ordinary schema-2 layout flush writes snapshot before current-schema metadata");
    const QJsonObject migratedRoot = QJsonDocument::fromJson(
        readBytes(schema2 + "/session.json")).object();
    const QJsonObject migratedDocument = migratedRoot.value("documents").toArray()
                                                   .first().toObject();
    const QString migratedSnapshot = migratedDocument.value("snapshot").toString();
    expect(migratedRoot.value("schemaVersion").toInt() == SessionManager::SchemaVersion &&
               migratedSnapshot != fixedSnapshot && migratedSnapshot.count('-') == 1 &&
               readBytes(schema2 + "/" + migratedSnapshot) ==
                   QStringLiteral("legacy 雪\rlegacy\r").toUtf8(),
           "schema-2 layout flush stores canonical UTF-8 at a content-addressed path");

    SessionManager freshReader(nullptr, schema2, 1);
    freshReader.loadSession();
    const auto freshDocuments = freshReader.documents();
    expect(!freshReader.writesBlocked() && freshDocuments.size() == 1 &&
               freshDocuments.first().encoding == TextEncoding::Utf16Le &&
               freshDocuments.first().eol == EolKind::Cr &&
               freshReader.readRecoveryContent(freshDocuments.first()).content ==
                   QStringLiteral("legacy 雪\rlegacy\r").toUtf8(),
           "fresh schema-4 reload keeps schema-2 content and disk format metadata");
    qputenv("NPP_SESSION_DIR", schema2.toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *editor = activeEditor(window.get());
    expect(editor && EditorUtils::text(editor) == QStringLiteral("legacy 雪\rlegacy\r").toUtf8() &&
               window->statusBar()->currentMessage().contains("UTF-16 LE") &&
               window->statusBar()->currentMessage().contains("Macintosh (CR)"),
           "schema-2 dirty recovery detects its legacy encoding and EOL after migration");
    QString error;
    expect(saveCurrentFileInMainWindow(window.get(), &error) &&
               readBytes(target).startsWith(QByteArray::fromHex("fffe")),
           "schema-2 recovered document preserves detected encoding on explicit save");
    window.reset();
    qunsetenv("NPP_SESSION_DIR");

    const QString invalidSchema2 = temporary.filePath("schema2-invalid");
    QDir().mkpath(invalidSchema2 + "/snapshots");
    const QString invalidId = QStringLiteral("schema2-invalid");
    const QString invalidSnapshot = QStringLiteral("snapshots/%1.snapshot").arg(QString::fromLatin1(
        QCryptographicHash::hash(invalidId.toUtf8(), QCryptographicHash::Sha256).toHex()));
    const QByteArray invalidBytes("binary\0payload", 14);
    writeBytes(invalidSchema2 + "/" + invalidSnapshot, invalidBytes);
    const QJsonObject invalidDocument{{"id", invalidId}, {"filePath", ""},
                                      {"untitledNumber", 1}, {"dirty", true},
                                      {"snapshot", invalidSnapshot}, {"originalExisted", false},
                                      {"originalSize", -1}, {"originalMtimeMs", -1},
                                      {"originalSha256", ""}};
    const QByteArray invalidMetadata = QJsonDocument(QJsonObject{
        {"schemaVersion", 2}, {"activeDocumentId", invalidId},
        {"documents", QJsonArray{invalidDocument}}}).toJson();
    writeBytes(invalidSchema2 + "/session.json", invalidMetadata);
    SessionManager invalidReader(nullptr, invalidSchema2, 1);
    invalidReader.loadSession();
    expect(invalidReader.writesBlocked() &&
               invalidReader.flush() == CheckpointStatus::WritesBlocked &&
               readBytes(invalidSchema2 + "/session.json") == invalidMetadata &&
               readBytes(invalidSchema2 + "/" + invalidSnapshot) == invalidBytes,
           "failed schema-2 canonicalization blocks writes and preserves source data");

    const QString schema3 = temporary.filePath("schema3-tampered");
    QDir().mkpath(schema3 + "/snapshots");
    const QString id3 = QStringLiteral("schema3-addressed");
    const QByteArray originalSnapshot("original schema3 bytes");
    const QString addressedSnapshot = QStringLiteral("snapshots/%1-%2.snapshot").arg(
        QString::fromLatin1(QCryptographicHash::hash(id3.toUtf8(), QCryptographicHash::Sha256).toHex()),
        QString::fromLatin1(QCryptographicHash::hash(originalSnapshot, QCryptographicHash::Sha256).toHex()));
    const QByteArray tampered("tampered valid utf8");
    writeBytes(schema3 + "/" + addressedSnapshot, tampered);
    const QJsonObject schema3Document{{"id", id3}, {"filePath", ""}, {"untitledNumber", 1},
                                      {"dirty", true}, {"snapshot", addressedSnapshot},
                                      {"originalExisted", false}, {"originalSize", -1},
                                      {"originalMtimeMs", -1}, {"originalSha256", ""}};
    const QJsonObject dual{{"primary", QJsonArray{id3}}, {"secondary", QJsonArray{}},
                           {"activePane", "primary"}, {"orientation", "horizontal"},
                           {"sizes", QJsonArray{}}};
    const QByteArray metadata = QJsonDocument(QJsonObject{{"schemaVersion", 3},
        {"activeDocumentId", id3}, {"documents", QJsonArray{schema3Document}},
        {"dualView", dual}}).toJson();
    const QString metadataPath = schema3 + "/session.json";
    writeBytes(metadataPath, metadata);
    SessionManager tamperedReader(nullptr, schema3, 1);
    tamperedReader.loadSession();
    expect(tamperedReader.writesBlocked() &&
               tamperedReader.flush() == CheckpointStatus::WritesBlocked,
           "schema-3 content-addressed snapshot tampering blocks migration");
    expect(readBytes(metadataPath) == metadata &&
               readBytes(schema3 + "/" + addressedSnapshot) == tampered,
           "failed schema-3 integrity check preserves metadata and snapshot bytes");
}

void testEncodedPreSchemaLegacyMigration()
{
    using namespace DocumentFormat;
    QTemporaryDir root;
    const QString canonical = root.filePath("canonical");
    const QString legacy = root.filePath("legacy");
    QDir().mkpath(legacy);
    QByteArray utf16;
    expect(encode(QStringLiteral("legacy 雪\r\n").toUtf8(), TextEncoding::Utf16Be, &utf16),
           "pre-schema UTF-16 fixture encodes");
    const QByteArray cp1252("price \x80\rprice\r", 14);
    const QString utf16Backup = legacy + "/new_1.backup";
    const QString cp1252Backup = legacy + "/new_2.backup";
    writeBytes(utf16Backup, utf16);
    writeBytes(cp1252Backup, cp1252);
    const QByteArray legacyMetadata = QJsonDocument(QJsonObject{
        {"activeTab", 2},
        {"untitledTabs", QJsonArray{
            QJsonObject{{"tabNumber", 1}, {"backupPath", utf16Backup}},
            QJsonObject{{"tabNumber", 2}, {"backupPath", cp1252Backup}}}}}).toJson();
    writeBytes(legacy + "/session.json", legacyMetadata);

    SessionManager importer(nullptr, canonical, 1, {legacy});
    importer.loadSession();
    expect(!importer.writesBlocked() && importer.documents().size() == 2 &&
               importer.flush() == CheckpointStatus::Durable,
           "encoded pre-schema backups import and flush transactionally");
    SessionManager fresh(nullptr, canonical, 1);
    fresh.loadSession();
    const auto documents = fresh.documents();
    expect(!fresh.writesBlocked() && documents.size() == 2,
           "encoded pre-schema import survives a fresh schema-4 reload");
    if (documents.size() == 2) {
        expect(documents.at(0).encoding == TextEncoding::Utf16Be &&
                   documents.at(0).eol == EolKind::CrLf &&
                   fresh.readRecoveryContent(documents.at(0)).content ==
                       QStringLiteral("legacy 雪\r\n").toUtf8(),
               "pre-schema UTF-16 content and format metadata are preserved");
        expect(documents.at(1).encoding == TextEncoding::Windows1252 &&
                   documents.at(1).eol == EolKind::Cr &&
                   fresh.readRecoveryContent(documents.at(1)).content ==
                       QStringLiteral("price €\rprice\r").toUtf8(),
               "pre-schema CP1252 content and format metadata are preserved");
    }
    expect(readBytes(utf16Backup) == utf16 && readBytes(cp1252Backup) == cp1252 &&
               readBytes(legacy + "/session.json") == legacyMetadata,
           "successful pre-schema import leaves source backups and metadata untouched");

    const QString failedCanonical = root.filePath("failed-canonical");
    const QString failedLegacy = root.filePath("failed-legacy");
    QDir().mkpath(failedLegacy);
    const QString badBackup = failedLegacy + "/new_3.backup";
    const QByteArray badBytes("binary\0payload", 14);
    writeBytes(badBackup, badBytes);
    const QByteArray badMetadata = QJsonDocument(QJsonObject{
        {"activeTab", 3}, {"untitledTabs", QJsonArray{
            QJsonObject{{"tabNumber", 3}, {"backupPath", badBackup}}}}}).toJson();
    writeBytes(failedLegacy + "/session.json", badMetadata);
    SessionManager failed(nullptr, failedCanonical, 1, {failedLegacy});
    failed.loadSession();
    expect(failed.writesBlocked() && failed.flush() == CheckpointStatus::WritesBlocked &&
               readBytes(badBackup) == badBytes &&
               readBytes(failedLegacy + "/session.json") == badMetadata &&
               !QFileInfo::exists(failedCanonical + "/session.json"),
           "failed pre-schema decode blocks writes and preserves all source data");

    const QString malformedCanonical = root.filePath("malformed-canonical");
    const QString malformedLegacy = root.filePath("malformed-legacy");
    QDir().mkpath(malformedLegacy);
    const QString validBackup = malformedLegacy + "/new_4.backup";
    writeBytes(validBackup, "valid legacy text");
    const QByteArray malformedMetadata = QJsonDocument(QJsonObject{
        {"activeTab", 4}, {"untitledTabs", QJsonArray{
            QJsonObject{{"tabNumber", 4}, {"backupPath", validBackup}},
            QJsonObject{{"tabNumber", "5"}, {"backupPath", validBackup}}}}}).toJson();
    writeBytes(malformedLegacy + "/session.json", malformedMetadata);
    SessionManager malformed(nullptr, malformedCanonical, 1, {malformedLegacy});
    malformed.loadSession();
    expect(malformed.writesBlocked() &&
               malformed.flush() == CheckpointStatus::WritesBlocked &&
               readBytes(malformedLegacy + "/session.json") == malformedMetadata &&
               readBytes(validBackup) == "valid legacy text" &&
               !QFileInfo::exists(malformedCanonical + "/session.json"),
           "malformed pre-schema manifest blocks partial migration and preserves sources");
}

void testCorruptRecoveryIsPreserved()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    SessionManager writer(nullptr, temporary.path(), 1);
    writer.loadSession();
    writer.updateDocument({"corrupt", {}, 1, true, TextEncoding::Utf8,
                           EolKind::None, EolKind::Lf}, "valid");
    writer.setSessionLayout({"corrupt"}, "corrupt");
    writer.setDualViewLayout({"corrupt"}, {}, "primary", Qt::Horizontal, {});
    expect(writer.flush() == CheckpointStatus::Durable, "corrupt-snapshot fixture is initially durable");
    const QJsonObject root = QJsonDocument::fromJson(
        readBytes(temporary.filePath("session.json"))).object();
    const QString relative = root.value("documents").toArray().first().toObject()
                                 .value("snapshot").toString();
    const QString snapshot = temporary.filePath(relative);
    const QByteArray corrupt("bad\0\xff", 5);
    writeBytes(snapshot, corrupt);
    SessionManager reader(nullptr, temporary.path(), 1);
    reader.loadSession();
    expect(reader.writesBlocked() && !reader.documents().isEmpty() &&
               reader.documents().first().recoveryState == RecoveryState::SnapshotUnreadable,
           "corrupt canonical UTF-8 recovery snapshot is rejected conservatively");
    expect(reader.flush() == CheckpointStatus::WritesBlocked && readBytes(snapshot) == corrupt,
           "corrupt recovery snapshot and metadata remain untouched");
}

void testContentAddressedRecoveryTamperingIsPreserved()
{
    using namespace DocumentFormat;
    const QList<QByteArray> tamperedPayloads{
        QByteArrayLiteral("changed valid utf8"),
        QByteArrayLiteral("canonical snap"),
        QByteArray("canonical\0snapshot", 18)
    };
    const char *messages[] = {
        "changed valid UTF-8 snapshot fails its content hash",
        "truncated valid UTF-8 snapshot fails its content hash",
        "NUL-bearing snapshot is rejected as non-text"
    };
    for (int i = 0; i < tamperedPayloads.size(); ++i) {
        QTemporaryDir temporary;
        SessionManager writer(nullptr, temporary.path(), 1);
        writer.loadSession();
        writer.updateDocument({"tampered", {}, 1, true, TextEncoding::Utf8,
                               EolKind::None, EolKind::Lf}, "canonical snapshot");
        writer.setSessionLayout({"tampered"}, "tampered");
        writer.setDualViewLayout({"tampered"}, {}, "primary", Qt::Horizontal, {});
        expect(writer.flush() == CheckpointStatus::Durable,
               "content-addressed tampering fixture is initially durable");
        const QString metadataPath = temporary.filePath("session.json");
        const QByteArray metadata = readBytes(metadataPath);
        const QJsonObject root = QJsonDocument::fromJson(metadata).object();
        const QString relative = root.value("documents").toArray().first().toObject()
                                     .value("snapshot").toString();
        const QString snapshot = temporary.filePath(relative);
        writeBytes(snapshot, tamperedPayloads.at(i));

        SessionManager reader(nullptr, temporary.path(), 1);
        reader.loadSession();
        expect(reader.writesBlocked() && !reader.documents().isEmpty() &&
                   reader.documents().first().recoveryState == RecoveryState::SnapshotUnreadable,
               messages[i]);
        const RecoveryReadResult recovered = reader.readRecoveryContent(reader.documents().first());
        expect(!recovered.success && recovered.content.isEmpty(),
               "tampered recovery bytes are never exposed as editable content");
        expect(reader.flush() == CheckpointStatus::WritesBlocked &&
                   readBytes(metadataPath) == metadata &&
                   readBytes(snapshot) == tamperedPayloads.at(i),
               "tampered snapshot and metadata are preserved byte-for-byte");
    }

    QTemporaryDir changedOriginal;
    const QString originalPath = changedOriginal.filePath("original.txt");
    writeBytes(originalPath, "original");
    SessionManager writer(nullptr, changedOriginal.path(), 1);
    writer.loadSession();
    writer.updateDocument({"named-tampered", originalPath, 0, true, TextEncoding::Utf8,
                           EolKind::None, EolKind::Lf}, "dirty recovery");
    writer.setSessionLayout({"named-tampered"}, "named-tampered");
    writer.setDualViewLayout({"named-tampered"}, {}, "primary", Qt::Horizontal, {});
    expect(writer.flush() == CheckpointStatus::Durable,
           "changed-original tampering fixture is durable");
    const QString metadataPath = changedOriginal.filePath("session.json");
    const QByteArray metadata = readBytes(metadataPath);
    const QString relative = QJsonDocument::fromJson(metadata).object().value("documents")
                                 .toArray().first().toObject().value("snapshot").toString();
    writeBytes(originalPath, "external change");
    writeBytes(changedOriginal.filePath(relative), "valid utf8 tamper");
    SessionManager reader(nullptr, changedOriginal.path(), 1);
    reader.loadSession();
    expect(reader.writesBlocked() &&
               reader.documents().first().recoveryState == RecoveryState::SnapshotUnreadable,
           "snapshot integrity failure outranks an externally changed original");
    expect(reader.flush() == CheckpointStatus::WritesBlocked &&
               readBytes(metadataPath) == metadata,
           "tampering with a changed original still preserves metadata");
}

void testMalformedRequiredRecoveryMetadataIsPreserved()
{
    const QString id = QStringLiteral("typed-metadata");
    const QByteArray snapshotBytes("recover me");
    const QString idHash = QString::fromLatin1(
        QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString contentHash = QString::fromLatin1(
        QCryptographicHash::hash(snapshotBytes, QCryptographicHash::Sha256).toHex());
    const QString snapshot = QStringLiteral("snapshots/%1-%2.snapshot").arg(idHash, contentHash);
    const QJsonObject validDocument{{"id", id}, {"filePath", ""}, {"untitledNumber", 1},
                                    {"dirty", true}, {"snapshot", snapshot},
                                    {"originalExisted", false}, {"originalSize", -1},
                                    {"originalMtimeMs", -1}, {"originalSha256", ""},
                                    {"encoding", "utf8"}, {"eol", "none"},
                                    {"insertionEol", "lf"}};
    const QJsonObject dual{{"primary", QJsonArray{id}}, {"secondary", QJsonArray{}},
                           {"activePane", "primary"}, {"orientation", "horizontal"},
                           {"sizes", QJsonArray{}}};
    const QList<QPair<QString, QJsonValue>> malformedFields{
        {"id", 7}, {"filePath", true}, {"untitledNumber", "1"},
        {"untitledNumber", 1.5}, {"dirty", "true"}, {"snapshot", 9},
        {"originalExisted", "false"}, {"originalSize", "-1"},
        {"originalSize", 1.5}, {"originalMtimeMs", "-1"},
        {"originalMtimeMs", 2.5}, {"originalSha256", false},
        {"originalSha256", "zz"}
    };
    for (int i = 0; i < malformedFields.size(); ++i) {
        QTemporaryDir temporary;
        QDir().mkpath(temporary.filePath("snapshots"));
        writeBytes(temporary.filePath(snapshot), snapshotBytes);
        QJsonObject document = validDocument;
        document.insert(malformedFields.at(i).first, malformedFields.at(i).second);
        const QByteArray metadata = QJsonDocument(QJsonObject{{"schemaVersion", 4},
            {"activeDocumentId", id}, {"documents", QJsonArray{document}},
            {"dualView", dual}}).toJson();
        const QString metadataPath = temporary.filePath("session.json");
        writeBytes(metadataPath, metadata);

        SessionManager reader(nullptr, temporary.path(), 1);
        reader.loadSession();
        expect(reader.writesBlocked() && reader.flush() == CheckpointStatus::WritesBlocked,
               "schema-4 required metadata rejects wrong types and non-integral values");
        expect(readBytes(metadataPath) == metadata && readBytes(temporary.filePath(snapshot)) == snapshotBytes,
               "malformed schema-4 metadata and recovery bytes are preserved");
    }

    QTemporaryDir migration;
    QDir().mkpath(migration.filePath("snapshots"));
    const QString legacySnapshot = QStringLiteral("snapshots/%1.snapshot").arg(idHash);
    writeBytes(migration.filePath(legacySnapshot), snapshotBytes);
    QJsonObject legacyDocument = validDocument;
    legacyDocument.remove("encoding");
    legacyDocument.remove("eol");
    legacyDocument.remove("insertionEol");
    legacyDocument.insert("snapshot", legacySnapshot);
    legacyDocument.insert("dirty", "true");
    const QByteArray legacyMetadata = QJsonDocument(QJsonObject{{"schemaVersion", 3},
        {"activeDocumentId", id}, {"documents", QJsonArray{legacyDocument}},
        {"dualView", dual}}).toJson();
    const QString legacyMetadataPath = migration.filePath("session.json");
    writeBytes(legacyMetadataPath, legacyMetadata);
    SessionManager legacyReader(nullptr, migration.path(), 1);
    legacyReader.loadSession();
    expect(legacyReader.writesBlocked() &&
               legacyReader.flush() == CheckpointStatus::WritesBlocked,
           "schema-3 migration rejects string dirty instead of hiding recovery");
    expect(readBytes(legacyMetadataPath) == legacyMetadata &&
               readBytes(migration.filePath(legacySnapshot)) == snapshotBytes,
           "failed schema migration preserves original metadata and recovery bytes");
}

void testMalformedSchema4CannotShiftFormatMetadata()
{
    using namespace DocumentFormat;
    QTemporaryDir temporary;
    const QString validPath = temporary.filePath("valid.txt");
    QByteArray validBytes;
    expect(encode("valid", TextEncoding::Utf16Be, &validBytes), "malformed-session fixture encodes");
    writeBytes(validPath, validBytes);
    QJsonObject malformed{{"id", "valid"}, {"filePath", ""}, {"untitledNumber", 0},
                          {"dirty", false}, {"encoding", "windows-1252"},
                          {"eol", "cr"}, {"insertionEol", "cr"}};
    const QFileInfo validInfo(validPath);
    QJsonObject valid{{"id", "valid"}, {"filePath", validPath}, {"untitledNumber", 0},
                      {"dirty", false}, {"snapshot", ""}, {"originalExisted", true},
                      {"originalSize", double(validBytes.size())},
                      {"originalMtimeMs", double(validInfo.lastModified().toMSecsSinceEpoch())},
                      {"originalSha256", QString::fromLatin1(
                           QCryptographicHash::hash(validBytes, QCryptographicHash::Sha256).toHex())},
                      {"encoding", "utf16-be"},
                      {"eol", "none"}, {"insertionEol", "lf"}};
    QJsonObject dual{{"primary", QJsonArray{"valid"}}, {"secondary", QJsonArray{}},
                     {"activePane", "primary"}, {"orientation", "horizontal"},
                     {"sizes", QJsonArray{}}};
    const QByteArray original = QJsonDocument(QJsonObject{{"schemaVersion", 4},
        {"activeDocumentId", "valid"}, {"documents", QJsonArray{valid, malformed}},
        {"dualView", dual}}).toJson();
    writeBytes(temporary.filePath("session.json"), original);
    SessionManager reader(nullptr, temporary.path(), 1);
    reader.loadSession();
    expect(reader.writesBlocked() && reader.documents().size() == 1 &&
               reader.documents().first().id == "valid" &&
               reader.documents().first().encoding == TextEncoding::Utf16Be,
           "duplicate schema-4 entries cannot replace the surviving logical document format");
    expect(reader.flush() == CheckpointStatus::WritesBlocked &&
               readBytes(temporary.filePath("session.json")) == original,
           "malformed schema-4 metadata is retained byte-for-byte");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("NppEncodingEolTests");
    QCoreApplication::setApplicationName("NppEncodingEolTests");
    testDetectionAndRoundTrips();
    testAtomicFailureAndEols();
    testWindowMenusSaveAndClones();
    testFreshLfAndCp1252FailureStaysDirty();
    testCleanRecoveryUsesCurrentDiskFormat();
    testRecoveryMetadataAndSchema3Migration();
    testLegacySnapshotCompatibilityAndIntegrity();
    testEncodedPreSchemaLegacyMigration();
    testCorruptRecoveryIsPreserved();
    testContentAddressedRecoveryTamperingIsPreserved();
    testMalformedRequiredRecoveryMetadataIsPreserved();
    testMalformedSchema4CannotShiftFormatMetadata();
    return failures == 0 ? 0 : 1;
}
