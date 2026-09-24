#include "editorpreferences.h"
#include "languagecatalog.h"
#include "ScintillaEditBase.h"
#include "ILexer.h"
#include "SciLexer.h"
#include "Lexilla.h"
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontComboBox>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <cstdio>

namespace {
int failures = 0;
void expect(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
QAction *action(QObject *root, const char *name) { return root->findChild<QAction *>(name); }
ScintillaEditBase *editorAt(QTabWidget *tabs, int index = -1) {
    if (!tabs) return nullptr;
    QWidget *page = tabs->widget(index < 0 ? tabs->currentIndex() : index);
    return page ? page->findChild<ScintillaEditBase *>() : nullptr;
}
void process() { QApplication::processEvents(); }
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path); expect(file.open(QIODevice::WriteOnly), "fixture file opens");
    expect(file.write(bytes) == bytes.size(), "fixture file writes");
}

void testPreferenceDefaultsAndPersistence()
{
    QSettings settings;
    settings.clear();
    const EditorPreferences defaults = EditorPreferencesStore::load(settings);
    expect(!defaults.fontFamily.isEmpty() && defaults.fontSize >= 6,
           "preferences provide a usable default font");
    expect(defaults.tabWidth == 4 && !defaults.useTabs,
           "preferences default to four-space indentation");
    expect(!defaults.wordWrap && defaults.lineNumbers && !defaults.showWhitespace &&
               !defaults.indentGuides,
           "view preference defaults are explicit");
    expect(defaults.defaultEol == DocumentFormat::EolKind::Lf &&
               defaults.defaultEncoding == DocumentFormat::TextEncoding::Utf8,
           "new-document format defaults are UTF-8 and LF");
    expect(defaults.toolbarVisible && defaults.statusBarVisible && defaults.rememberWindowState,
           "window chrome defaults preserve current behavior");

    EditorPreferences changed = defaults;
    changed.fontFamily = QStringLiteral("Monospace");
    changed.fontSize = 15;
    changed.tabWidth = 7;
    changed.useTabs = true;
    changed.wordWrap = true;
    changed.lineNumbers = false;
    changed.showWhitespace = true;
    changed.indentGuides = true;
    changed.defaultEol = DocumentFormat::EolKind::CrLf;
    changed.defaultEncoding = DocumentFormat::TextEncoding::Utf8Bom;
    changed.toolbarVisible = false;
    changed.statusBarVisible = false;
    changed.rememberWindowState = false;
    EditorPreferencesStore::save(settings, changed);
    settings.sync();
    expect(EditorPreferencesStore::load(settings) == changed,
           "all functioning preferences persist through QSettings");
    settings.setValue("preferences/defaultEol", 999);
    settings.setValue("preferences/defaultEncoding", -1);
    settings.setValue("preferences/fontFamily", QString());
    settings.setValue("preferences/fontSize", QStringLiteral("large"));
    settings.setValue("preferences/tabWidth", QStringLiteral("wide"));
    settings.setValue("preferences/lineNumbers", QStringLiteral("garbage"));
    settings.setValue("preferences/useTabs", QStringLiteral("garbage"));
    settings.setValue("preferences/wordWrap", QStringLiteral("garbage"));
    settings.setValue("preferences/toolbarVisible", QStringLiteral("garbage"));
    settings.setValue("preferences/statusBarVisible", QStringLiteral("garbage"));
    settings.setValue("preferences/rememberWindowState", QStringLiteral("garbage"));
    const EditorPreferences repaired = EditorPreferencesStore::load(settings);
    const EditorPreferences safeDefaults;
    expect(repaired.defaultEol == DocumentFormat::EolKind::Lf &&
               repaired.defaultEncoding == DocumentFormat::TextEncoding::Utf8 &&
               repaired.fontFamily == safeDefaults.fontFamily &&
               repaired.fontSize == safeDefaults.fontSize && repaired.tabWidth == safeDefaults.tabWidth &&
               repaired.lineNumbers == safeDefaults.lineNumbers &&
               repaired.useTabs == safeDefaults.useTabs && repaired.wordWrap == safeDefaults.wordWrap &&
               repaired.toolbarVisible == safeDefaults.toolbarVisible &&
               repaired.statusBarVisible == safeDefaults.statusBarVisible &&
               repaired.rememberWindowState == safeDefaults.rememberWindowState,
           "invalid persisted preference types and values fall back to safe defaults");
}

void testPreferencesApplyToRealEditor()
{
    ScintillaEditBase editor;
    expect(LanguageCatalog::apply(&editor, QStringLiteral("python")) &&
               editor.send(SCI_GETLEXER) == SCLEX_PYTHON,
           "preference fixture starts with an active lexer");
    EditorPreferences p;
    p.fontFamily = QStringLiteral("DejaVu Sans Mono");
    p.fontSize = 16;
    p.tabWidth = 6;
    p.useTabs = true;
    p.wordWrap = true;
    p.lineNumbers = false;
    p.showWhitespace = true;
    p.indentGuides = true;
    EditorPreferencesStore::apply(&editor, p);
    char font[128]{};
    editor.send(SCI_STYLEGETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(font));
    expect(QByteArray(font) == p.fontFamily.toUtf8() &&
               editor.send(SCI_STYLEGETSIZE, STYLE_DEFAULT) == p.fontSize &&
               editor.send(SCI_STYLEGETSIZE, 5) == p.fontSize &&
               editor.send(SCI_GETLEXER) == SCLEX_PYTHON,
           "fonts apply through lexer styles without resetting language");
    expect(editor.send(SCI_GETTABWIDTH) == 6 && editor.send(SCI_GETINDENT) == 6 &&
               editor.send(SCI_GETUSETABS),
           "tab width and tabs mode apply to the editor");
    expect(editor.send(SCI_GETWRAPMODE) == SC_WRAP_WORD &&
               editor.send(SCI_GETMARGINWIDTHN, 0) == 0 &&
               editor.send(SCI_GETVIEWWS) == SCWS_VISIBLEALWAYS &&
               editor.send(SCI_GETVIEWEOL) &&
               editor.send(SCI_GETINDENTATIONGUIDES) == SC_IV_LOOKFORWARD,
           "wrap, line numbers, whitespace, and indent guides apply to the editor");
}

void testLanguageCatalogAndLexerCreation()
{
    const QStringList expected = {"plain", "c", "cpp", "csharp", "java", "javascript",
        "json", "python", "html", "xml", "css", "bash", "sql", "properties", "yaml", "markdown"};
    expect(LanguageCatalog::ids() == expected,
           "language menu exposes the verified supported language set in stable order");
    expect(LanguageCatalog::detectForPath("demo.cpp") == "cpp" &&
               LanguageCatalog::detectForPath("demo.c") == "c" &&
               LanguageCatalog::detectForPath("demo.cs") == "csharp" &&
               LanguageCatalog::detectForPath("demo.java") == "java" &&
               LanguageCatalog::detectForPath("demo.js") == "javascript" &&
               LanguageCatalog::detectForPath("demo.json") == "json" &&
               LanguageCatalog::detectForPath("demo.py") == "python" &&
               LanguageCatalog::detectForPath("demo.html") == "html" &&
               LanguageCatalog::detectForPath("demo.xml") == "xml" &&
               LanguageCatalog::detectForPath("demo.css") == "css" &&
               LanguageCatalog::detectForPath("demo.sh") == "bash" &&
               LanguageCatalog::detectForPath("demo.sql") == "sql" &&
               LanguageCatalog::detectForPath("demo.ini") == "properties" &&
               LanguageCatalog::detectForPath("demo.yaml") == "yaml" &&
               LanguageCatalog::detectForPath("demo.md") == "markdown" &&
               LanguageCatalog::detectForPath("README.unknown") == "plain",
           "extensions map to user-facing languages with a plain fallback");
    auto *normalLexer = LanguageCatalog::createLexer("plain");
    expect(normalLexer != nullptr && LanguageCatalog::createLexer("not-a-language") == nullptr,
           "Normal Text creates Lexilla's null lexer while unknown languages fail safely");
    if (normalLexer) normalLexer->Release();
    for (const QString &id : expected) {
        if (id == "plain") continue;
        auto *lexer = LanguageCatalog::createLexer(id);
        expect(lexer != nullptr, qPrintable(QStringLiteral("Lexilla creates reliable lexer for %1").arg(id)));
        if (lexer) lexer->Release();
    }
}

void testPreferencesDialogApplyCancelAndEditorLifecycle()
{
    QSettings().clear();
    QTemporaryDir session;
    qputenv("NPP_SESSION_DIR", session.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show();
    process();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *preferencesAction = action(window.get(), "preferencesAction");
    expect(preferencesAction, "Settings/Preferences action is available");
    const int originalSize = int(editorAt(primary)->send(SCI_STYLEGETSIZE, STYLE_DEFAULT));

    preferencesAction->trigger();
    process();
    auto *dialog = window->findChild<QDialog *>("preferencesDialog");
    expect(dialog && dialog->isVisible(), "Preferences opens a native dialog");
    auto *firstFontSize = dialog ? dialog->findChild<QSpinBox *>("fontSizeSpinBox") : nullptr;
    firstFontSize->setValue(originalSize + 5);
    auto *firstButtons = dialog->findChild<QDialogButtonBox *>();
    firstButtons->button(QDialogButtonBox::Cancel)->click();
    process();
    auto *afterCancelEditor = editorAt(primary);
    const auto afterCancelSize = afterCancelEditor->send(SCI_STYLEGETSIZE, STYLE_DEFAULT);
    expect(afterCancelSize == originalSize &&
               !QSettings().contains("preferences/fontSize"),
           "Cancel neither applies nor persists edited values");

    auto *cloneAction = action(window.get(), "cloneToOtherViewAction");
    cloneAction->trigger();
    process();
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    preferencesAction->trigger(); process();
    dialog = window->findChild<QDialog *>("preferencesDialog");
    dialog->findChild<QFontComboBox *>("fontFamilyComboBox")->setCurrentFont(QFont("DejaVu Sans Mono"));
    dialog->findChild<QSpinBox *>("fontSizeSpinBox")->setValue(14);
    dialog->findChild<QSpinBox *>("tabWidthSpinBox")->setValue(8);
    dialog->findChild<QCheckBox *>("useTabsCheckBox")->setChecked(true);
    dialog->findChild<QCheckBox *>("wordWrapCheckBox")->setChecked(true);
    dialog->findChild<QCheckBox *>("lineNumbersCheckBox")->setChecked(false);
    dialog->findChild<QCheckBox *>("showWhitespaceCheckBox")->setChecked(true);
    dialog->findChild<QCheckBox *>("indentGuidesCheckBox")->setChecked(true);
    dialog->findChild<QCheckBox *>("toolbarVisibleCheckBox")->setChecked(false);
    dialog->findChild<QCheckBox *>("statusBarVisibleCheckBox")->setChecked(false);
    dialog->findChild<QComboBox *>("defaultEolComboBox")->setCurrentIndex(1);
    dialog->findChild<QComboBox *>("defaultEncodingComboBox")->setCurrentIndex(1);
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); process();
    for (auto *view : {editorAt(primary), editorAt(secondary)}) {
        expect(view->send(SCI_STYLEGETSIZE, STYLE_DEFAULT) == 14 &&
                   view->send(SCI_GETTABWIDTH) == 8 && view->send(SCI_GETUSETABS) &&
                   view->send(SCI_GETWRAPMODE) == SC_WRAP_WORD &&
                   view->send(SCI_GETMARGINWIDTHN, 0) == 0 &&
                   view->send(SCI_GETVIEWWS) == SCWS_VISIBLEALWAYS &&
                   view->send(SCI_GETVIEWEOL) &&
                   view->send(SCI_GETINDENTATIONGUIDES) == SC_IV_LOOKFORWARD,
               "Apply updates every existing editor including clones");
    }
    expect(QSettings().value("preferences/fontSize").toInt() == 14 &&
               window->findChild<QToolBar *>("MainToolBar")->isHidden() &&
               window->statusBar()->isHidden(),
           "Apply persists preferences and updates window chrome");
    const QByteArray extraLines("one\ntwo\nthree\n");
    editorAt(primary)->send(SCI_APPENDTEXT, extraLines.size(), reinterpret_cast<sptr_t>(extraLines.constData()));
    process();
    expect(editorAt(primary)->send(SCI_GETMARGINWIDTHN, 0) == 0,
           "hidden line numbers remain hidden after document changes");

    dialog->findChild<QSpinBox *>("fontSizeSpinBox")->setValue(13);
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click(); process();
    expect(!dialog->isVisible() && QSettings().value("preferences/fontSize").toInt() == 13,
           "OK applies, persists, and closes Preferences");
    action(window.get(), "newAction")->trigger(); process();
    auto *newEditor = editorAt(secondary);
    expect(newEditor->send(SCI_STYLEGETSIZE, STYLE_DEFAULT) == 13 &&
               newEditor->send(SCI_GETTABWIDTH) == 8 && newEditor->send(SCI_GETUSETABS) &&
               newEditor->send(SCI_GETEOLMODE) == SC_EOL_CRLF &&
               action(window.get(), "encodingUtf8BomAction")->isChecked(),
           "new editors inherit applied preferences and default document format");
    expect(window->statusBar()->currentMessage().isEmpty() || window->statusBar()->isHidden(),
           "status bar visibility remains consistently applied");
}

void testLanguageWorkflow()
{
    QSettings().clear();
    QTemporaryDir root;
    qputenv("NPP_SESSION_DIR", root.filePath("session").toUtf8());
    const QString cpp = root.filePath("sample.cpp");
    const QString json = root.filePath("sample.json");
    writeFile(cpp, "class Widget {};\n");
    writeFile(json, "{}\n");
    std::unique_ptr<QMainWindow> window(createMainWindow());
    QString error;
    expect(openFileInMainWindow(window.get(), cpp, &error), "C++ fixture opens");
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *cppAction = action(window.get(), "languageCppAction");
    auto *csharpAction = action(window.get(), "languageCsharpAction");
    auto *pythonAction = action(window.get(), "languagePythonAction");
    auto *jsonAction = action(window.get(), "languageJsonAction");
    auto *cssAction = action(window.get(), "languageCssAction");
    expect(cppAction && pythonAction && jsonAction && cppAction->isChecked(),
           "extension detection selects and checks C++");
    expect(editorAt(primary)->send(SCI_GETLEXER) == SCLEX_CPP,
           "detected C++ creates the verified Lexilla lexer");
    expect(editorAt(primary)->send(SCI_GETSTYLEAT, 0) == SCE_C_WORD,
           "C++ selection configures C++ keywords");

    const QByteArray csharpText("delegate void Handler();");
    editorAt(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(csharpText.constData()));
    csharpAction->trigger(); process();
    expect(editorAt(primary)->send(SCI_GETSTYLEAT, 0) == SCE_C_WORD,
           "C# selection configures C# keywords");

    pythonAction->trigger(); process();
    expect(pythonAction->isChecked() && !cppAction->isChecked() &&
               editorAt(primary)->send(SCI_GETLEXER) == SCLEX_PYTHON,
           "explicit language selection is mutually exclusive and applies its lexer");
    action(window.get(), "cloneToOtherViewAction")->trigger(); process();
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    expect(pythonAction->isChecked() && editorAt(secondary)->send(SCI_GETLEXER) == SCLEX_PYTHON,
           "explicit language belongs to the shared logical document and clones agree");
    const QString explicitSave = root.filePath("explicit.json");
    expect(saveCurrentFileAsInMainWindow(window.get(), explicitSave, &error) && pythonAction->isChecked(),
           "Save As never changes an explicit language override");
    action(window.get(), "moveToOtherViewAction")->trigger(); process();

    expect(openFileInMainWindow(window.get(), json, &error) && jsonAction->isChecked(),
           "opening another extension refreshes checked language state");
    const QString autoSave = root.filePath("automatic.css");
    expect(saveCurrentFileAsInMainWindow(window.get(), autoSave, &error) && cssAction->isChecked(),
           "Save As updates automatic language from the new extension");
    primary->setCurrentIndex(0); process();
    expect(pythonAction->isChecked(), "tab switching restores each logical document language check");
    primary->setCurrentIndex(1); process();
    expect(cssAction->isChecked(), "switching back restores the automatic language check");
}

void testToolbarViewSettingsStayGlobal()
{
    QSettings().clear();
    QTemporaryDir root;
    qputenv("NPP_SESSION_DIR", root.filePath("session").toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    action(window.get(), "cloneToOtherViewAction")->trigger(); process();
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *wrapAction = action(window.get(), "wordWrapAction");
    auto *charactersAction = action(window.get(), "showAllCharactersAction");
    auto *guidesAction = action(window.get(), "indentGuideAction");
    expect(wrapAction && charactersAction && guidesAction,
           "toolbar view actions have stable behavioral seams");
    if (!wrapAction || !charactersAction || !guidesAction) return;
    wrapAction->trigger();
    charactersAction->trigger();
    guidesAction->trigger();
    process();
    for (ScintillaEditBase *view : {editorAt(primary), editorAt(secondary)})
        expect(view->send(SCI_GETWRAPMODE) == SC_WRAP_WORD &&
                   view->send(SCI_GETVIEWWS) == SCWS_VISIBLEALWAYS && view->send(SCI_GETVIEWEOL) &&
                   view->send(SCI_GETINDENTATIONGUIDES) == SC_IV_LOOKFORWARD,
               "toolbar view settings update every clone");
    expect(QSettings().value("preferences/wordWrap").toBool() &&
               QSettings().value("preferences/showWhitespace").toBool() &&
               QSettings().value("preferences/indentGuides").toBool(),
           "toolbar view settings update persisted preferences");
    action(window.get(), "newAction")->trigger(); process();
    auto *newEditor = editorAt(secondary);
    expect(newEditor->send(SCI_GETWRAPMODE) == SC_WRAP_WORD &&
               newEditor->send(SCI_GETVIEWWS) == SCWS_VISIBLEALWAYS &&
               newEditor->send(SCI_GETINDENTATIONGUIDES) == SC_IV_LOOKFORWARD,
           "new editors inherit toolbar-updated view settings");
}

void testLanguageStateSurvivesCleanAndDirtyRecovery()
{
    QSettings().clear();
    QTemporaryDir root;
    qputenv("NPP_SESSION_DIR", root.filePath("session").toUtf8());
    const QString cpp = root.filePath("named.cpp");
    writeFile(cpp, "int main() {}\n");
    {
        std::unique_ptr<QMainWindow> window(createMainWindow());
        QString error;
        expect(openFileInMainWindow(window.get(), cpp, &error), "language recovery fixture opens");
        action(window.get(), "languagePythonAction")->trigger();
        auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
        const QByteArray dirty("def recovered():\n    pass\n");
        editorAt(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(dirty.constData()));
        window->close();
    }
    {
        std::unique_ptr<QMainWindow> restored(createMainWindow());
        expect(action(restored.get(), "languagePythonAction")->isChecked(),
               "explicit language survives dirty recovery");
        const QString json = root.filePath("renamed.json");
        QString error;
        expect(saveCurrentFileAsInMainWindow(restored.get(), json, &error) &&
                   action(restored.get(), "languagePythonAction")->isChecked(),
               "restored explicit language survives Save As");
        restored->close();
    }
    {
        std::unique_ptr<QMainWindow> clean(createMainWindow());
        expect(action(clean.get(), "languagePythonAction")->isChecked(),
               "explicit language survives a clean session restart");
        clean->close();
    }
}

}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("TextPinnaclePreferencesTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TextPinnaclePreferencesTests"));
    testPreferenceDefaultsAndPersistence();
    testPreferencesApplyToRealEditor();
    testLanguageCatalogAndLexerCreation();
    testPreferencesDialogApplyCancelAndEditorLifecycle();
    testLanguageWorkflow();
    testLanguageStateSurvivesCleanAndDirtyRecovery();
    testToolbarViewSettingsStayGlobal();
    return failures == 0 ? 0 : 1;
}
