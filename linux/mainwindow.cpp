#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QFile>
#include <QTextStream>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include "printhelper.h"
#include <QDockWidget>
#include <QFileSystemModel>
#include <QTreeView>
#include <QTreeWidget>
#include <QFileInfo>
#include <QTabWidget>
#include <QSplitter>
#include <QSharedPointer>
#include <QCloseEvent>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QLayout>
#include <QKeySequence>
#include <QIcon>
#include <QTimer>
#include <QSettings>
#include <QSet>
#include <QDir>
#include <QByteArray>
#include <QUuid>
#include "ScintillaEditBase.h"
#include "findreplace.h"
#include "sessionmanager.h"
#include "functionlist.h"
#include "documentlist.h"
#include "filebrowser.h"
#include "documentmap.h"
#include "editorutils.h"
#include "macromanager.h"
#include "dualviewmanager.h"
#include "documentformat.h"

// Include Scintilla ILexer header before Lexilla.h
#include "ILexer.h"
// Include Lexilla headers
#include "Lexilla.h"
#include "SciLexer.h"

struct LogicalDocumentState {
    QString filePath;
    bool modified = false;
    int tabNumber = 0;
    QString id;
    QString recoveryWarning;
    bool recoveryCheckpointBlocked = false;
    bool requiresExplicitSave = false;
    SessionManager *sessionManager = nullptr;
    DocumentFormat::TextEncoding encoding = DocumentFormat::TextEncoding::Utf8;
    DocumentFormat::EolKind eol = DocumentFormat::EolKind::None;
    DocumentFormat::EolKind insertionEol = DocumentFormat::EolKind::Lf;
};

class DocumentTab : public QWidget {
    Q_OBJECT

public:
    DocumentTab(const QString& filePath = "", int tabNumber = 0,
                const QString& documentId = QString(), QWidget* parent = nullptr,
                QSharedPointer<LogicalDocumentState> shared = {})
        : QWidget(parent), m_state(shared ? shared : QSharedPointer<LogicalDocumentState>::create()) {
        if (!shared) {
            m_state->filePath = filePath;
            m_state->tabNumber = tabNumber;
            m_state->id = documentId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                               : documentId;
        }
        setupUI();
        setupActions();
        updateTitle();
        setupLineNumbers();
    }
    ~DocumentTab() override {
        if (editor)
            editor->disconnect();
    }

    ScintillaEditBase* getEditor() { return editor; }
    QString getFilePath() const { return m_state->filePath; }
    bool isDirty() const { return m_state->modified; }
    void setDirty(bool dirty) { m_state->modified = dirty; updateTitle(); }
    void setRecoveredDirty(bool dirty) {
        m_state->requiresExplicitSave = dirty;
        setDirty(dirty);
    }
    void markExplicitlySaved() { m_state->requiresExplicitSave = false; }
    void setFilePath(const QString& path) {
        m_state->filePath = path;
        updateTitle();
        // Apply lexer when file path changes (e.g., after Save As)
        applyLexer();
    }
    int getTabNumber() const { return m_state->tabNumber; }
    bool isDisposablePlaceholder() const {
        return m_state->filePath.isEmpty() && !m_state->modified && editor->send(SCI_GETLENGTH) == 0;
    }
    QString documentId() const { return m_state->id; }
    QString recoveryWarning() const { return m_state->recoveryWarning; }
    void setRecoveryWarning(const QString &warning) { m_state->recoveryWarning = warning; }
    void setRecoveryCheckpointBlocked(bool blocked) { m_state->recoveryCheckpointBlocked = blocked; }
    void setSessionManager(SessionManager* sessionManager) { m_state->sessionManager = sessionManager; }
    QSharedPointer<LogicalDocumentState> sharedState() const { return m_state; }
    DocumentFormat::TextEncoding encoding() const { return m_state->encoding; }
    DocumentFormat::EolKind eol() const { return m_state->eol; }
    DocumentFormat::EolKind insertionEol() const { return m_state->insertionEol; }
    void setDocumentFormat(DocumentFormat::TextEncoding encoding,
                           DocumentFormat::EolKind eol,
                           DocumentFormat::EolKind insertion) {
        m_state->encoding = encoding;
        m_state->eol = eol;
        m_state->insertionEol = insertion;
        editor->send(SCI_SETEOLMODE, insertion == DocumentFormat::EolKind::CrLf ? SC_EOL_CRLF :
                                     insertion == DocumentFormat::EolKind::Cr ? SC_EOL_CR : SC_EOL_LF);
    }
    void refreshEolMetadata() {
        const auto info = DocumentFormat::scanEols(EditorUtils::text(editor));
        m_state->eol = info.kind;
    }
    void markMetadataDirty() {
        m_state->requiresExplicitSave = true;
        setDirty(true);
        checkpoint();
    }
    void checkpoint() {
        if (m_state->sessionManager && !m_state->recoveryCheckpointBlocked && !isDisposablePlaceholder()) {
            m_state->sessionManager->updateDocument(
                {m_state->id, m_state->filePath, m_state->tabNumber, m_state->modified,
                 m_state->encoding, m_state->eol, m_state->insertionEol},
                EditorUtils::text(editor));
        }
    }

private:
    void setupUI() {
        editor = new ScintillaEditBase(this);
        editor->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        editor->send(SCI_SETEOLMODE, SC_EOL_LF);

        // Set up layout
        QVBoxLayout* layout = new QVBoxLayout();
        layout->addWidget(editor);
        layout->setContentsMargins(0, 0, 0, 0);
        setLayout(layout);

        // Apply default styling
        setupDefaultStyling();
    }

    void setupActions() {
        // Connect editor signals to track modifications
        connect(editor, &ScintillaEditBase::savePointChanged, this, [this](bool dirty) {
            m_state->modified = dirty || m_state->requiresExplicitSave;
            if (dirty)
                m_state->recoveryCheckpointBlocked = false;
            updateTitle();
            checkpoint();
        });

        // Connect modification signal for session management
        connect(editor, &ScintillaEditBase::modified, this, &DocumentTab::onTextChanged);
    }

    void setupLineNumbers() {
        // Set up line number margin (margin 0)
        editor->send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
        editor->send(SCI_SETMARGINWIDTHN, 0, 20); // Initial width

        // Connect to document change signals to recalculate margin width
        connect(editor, &ScintillaEditBase::notify, this, &DocumentTab::onEditorNotify);
    }

    void updateTitle() {
        emit titleChanged();
    }

    void onTextChanged() {
        m_state->recoveryCheckpointBlocked = false;
        refreshEolMetadata();
        checkpoint();
    }

    void onEditorNotify(Scintilla::NotificationData *notification) {
        // Handle notifications to update margin width when line count changes
        if (notification->nmhdr.code == Scintilla::Notification::Modified) {
            // Check if we need to update margin width
            Scintilla::Position lineCount = editor->send(SCI_GETLINECOUNT);
            if (lineCount > 0) {
                // Calculate the width needed for line numbers
                int maxLineDigits = QString::number(lineCount).length();
                int charWidth = editor->send(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>("9"));
                int marginWidth = charWidth * (maxLineDigits + 1); // Add extra space for padding

                // Set the margin width
                editor->send(SCI_SETMARGINWIDTHN, 0, marginWidth);
            }
        }
    }

    void setupDefaultStyling() {
        // Set default text style
        editor->send(SCI_STYLESETFORE, STYLE_DEFAULT, 0x000000);  // Black text
        editor->send(SCI_STYLESETBACK, STYLE_DEFAULT, 0xFFFFFF);  // White background
        editor->send(SCI_STYLESETEOLFILLED, STYLE_DEFAULT, true);

        // Set default font
        editor->send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>("DejaVu Sans"));
        editor->send(SCI_STYLESETSIZE, STYLE_DEFAULT, 10);

        // Apply lexer if file has extension
        applyLexer();
    }

    void applyLexer() {
        // If we have a file path, determine the appropriate lexer
        if (!m_state->filePath.isEmpty()) {
            QString extension = QFileInfo(m_state->filePath).suffix().toLower();

            // Determine lexer based on extension
            QString lexerName = "null";  // Default to null lexer

            if (extension == "cpp" || extension == "cxx" || extension == "cc" ||
                extension == "c" || extension == "h" || extension == "hpp" ||
                extension == "hh") {
                lexerName = "cpp";
            } else if (extension == "py") {
                lexerName = "python";
            } else if (extension == "js" || extension == "mjs" || extension == "cjs") {
                lexerName = "javascript";
            } else if (extension == "json") {
                lexerName = "json";
            } else if (extension == "xml" || extension == "xhtml" || extension == "svg") {
                lexerName = "xml";
            } else if (extension == "html" || extension == "htm") {
                lexerName = "hypertext";
            } else if (extension == "css") {
                lexerName = "css";
            } else if (extension == "sh" || extension == "bash") {
                lexerName = "bash";
            } else if (extension == "ini" || extension == "cfg" || extension == "conf") {
                lexerName = "properties";
            } else if (extension == "yaml" || extension == "yml") {
                lexerName = "yaml";
            } else if (extension == "md" || extension == "markdown") {
                lexerName = "markdown";
            } else if (extension == "sql") {
                lexerName = "sql";
            } else if (extension == "java") {
                lexerName = "java";
            }

            // Create and apply the lexer
            Scintilla::ILexer5* lexer = CreateLexer(lexerName.toStdString().c_str());
            if (lexer) {
                editor->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));

                // Apply styling for the lexer
                setupLexerStyling(lexerName);
            }
        } else {
            // For untitled documents, use null lexer
            Scintilla::ILexer5* lexer = CreateLexer("null");
            if (lexer) {
                editor->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
                setupLexerStyling("null");
            }
        }
    }

    void setupLexerStyling(const QString& lexerName) {
        // Clear existing styles
        editor->send(SCI_CLEARDOCUMENTSTYLE);

        if (lexerName == "cpp") {
            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_C_WORD, 0x0000FF);  // Keyword color

            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_C_STRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_C_COMMENT, 0x808080);  // Comment color
            editor->send(SCI_STYLESETFORE, SCE_C_COMMENTLINE, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_C_NUMBER, 0xA52A2A);  // Number color

            // Operators
            editor->send(SCI_STYLESETFORE, SCE_C_OPERATOR, 0x000000);  // Operator color

            // Preprocessor
            editor->send(SCI_STYLESETFORE, SCE_C_PREPROCESSOR, 0x008080);  // Preprocessor color

            // Set keywords
            const char* cppKeywords = "auto break case char const continue default do double else enum "
                                      "extern float for goto if int long register return short signed sizeof "
                                      "static struct switch typedef union unsigned void volatile while";
            editor->send(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(cppKeywords));

        } else if (lexerName == "python") {
            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_P_WORD, 0x0000FF);  // Keyword color

            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_P_STRING, 0x008000);  // String color
            editor->send(SCI_STYLESETFORE, SCE_P_CHARACTER, 0x008000);  // Character color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_P_COMMENTLINE, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_P_NUMBER, 0xA52A2A);  // Number color

            // Operators
            editor->send(SCI_STYLESETFORE, SCE_P_OPERATOR, 0x000000);  // Operator color

            // Set keywords
            const char* pythonKeywords = "and as assert break class continue def del elif else "
                                         "except exec finally for from global if import in is "
                                         "lambda not or pass print raise return try while with yield";
            editor->send(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(pythonKeywords));

        } else if (lexerName == "javascript") {
            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_C_WORD, 0x0000FF);  // Keyword color

            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_C_STRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_C_COMMENT, 0x808080);  // Comment color
            editor->send(SCI_STYLESETFORE, SCE_C_COMMENTLINE, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_C_NUMBER, 0xA52A2A);  // Number color

            // Operators
            editor->send(SCI_STYLESETFORE, SCE_C_OPERATOR, 0x000000);  // Operator color

            // Set keywords
            const char* jsKeywords = "break case catch class const continue debugger default "
                                     "delete do else export extends false finally for function "
                                     "if import in instanceof let new null return super switch "
                                     "this throw true try typeof var void while with yield";
            editor->send(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(jsKeywords));

        } else if (lexerName == "json") {
            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_JSON_STRING, 0x008000);  // String color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_JSON_NUMBER, 0xA52A2A);  // Number color

            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_JSON_KEYWORD, 0x0000FF);  // Keyword color

            // Note: No comment styling for JSON in this version

        } else if (lexerName == "xml") {
            // Tags (blue)
            editor->send(SCI_STYLESETFORE, SCE_H_TAG, 0x0000FF);  // Tag color

            // Strings (green) - Use both double and single string styles
            editor->send(SCI_STYLESETFORE, SCE_H_DOUBLESTRING, 0x008000);  // String color
            editor->send(SCI_STYLESETFORE, SCE_H_SINGLESTRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_H_COMMENT, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_H_NUMBER, 0xA52A2A);  // Number color

        } else if (lexerName == "hypertext") {
            // Tags (blue)
            editor->send(SCI_STYLESETFORE, SCE_H_TAG, 0x0000FF);  // Tag color

            // Strings (green) - Use both double and single string styles
            editor->send(SCI_STYLESETFORE, SCE_H_DOUBLESTRING, 0x008000);  // String color
            editor->send(SCI_STYLESETFORE, SCE_H_SINGLESTRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_H_COMMENT, 0x808080);  // Comment color

        } else if (lexerName == "css") {
            // Selectors (blue)
            editor->send(SCI_STYLESETFORE, SCE_CSS_IDENTIFIER, 0x0000FF);  // Selector color

            // Properties (green) - Use identifier instead of property
            editor->send(SCI_STYLESETFORE, SCE_CSS_IDENTIFIER, 0x008000);  // Property color

            // Values (brown)
            editor->send(SCI_STYLESETFORE, SCE_CSS_VALUE, 0xA52A2A);  // Value color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_CSS_COMMENT, 0x808080);  // Comment color

        } else if (lexerName == "bash") {
            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_SH_WORD, 0x0000FF);  // Keyword color

            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_SH_STRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_SH_COMMENTLINE, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_SH_NUMBER, 0xA52A2A);  // Number color

            // Operators
            editor->send(SCI_STYLESETFORE, SCE_SH_OPERATOR, 0x000000);  // Operator color

            // Set keywords
            const char* bashKeywords = "if then else elif fi for while until do done case esac "
                                       "if then else elif fi for while until do done case esac "
                                       "function return";
            editor->send(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(bashKeywords));

        } else if (lexerName == "properties") {
            // Keys (blue)
            editor->send(SCI_STYLESETFORE, SCE_PROPS_KEY, 0x0000FF);  // Key color

            // Values (green) - Note: This version may not have a separate value style
            editor->send(SCI_STYLESETFORE, SCE_PROPS_KEY, 0x0000FF);  // Use key color for values if no separate style

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_PROPS_COMMENT, 0x808080);  // Comment color

        } else if (lexerName == "yaml") {
            // Keys (blue) - Use keyword instead of word
            editor->send(SCI_STYLESETFORE, SCE_YAML_KEYWORD, 0x0000FF);  // Key color

            // Strings (green) - Use text instead of string
            editor->send(SCI_STYLESETFORE, SCE_YAML_TEXT, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_YAML_COMMENT, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_YAML_NUMBER, 0xA52A2A);  // Number color

        } else if (lexerName == "markdown") {
            // Headers (blue) - Using available header styles
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_HEADER1, 0x0000FF);  // Header color
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_HEADER2, 0x0000FF);  // Header color
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_HEADER3, 0x0000FF);  // Header color
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_HEADER4, 0x0000FF);  // Header color
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_HEADER5, 0x0000FF);  // Header color
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_HEADER6, 0x0000FF);  // Header color

            // Links (green)
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_LINK, 0x008000);  // Link color

            // Code blocks (brown)
            editor->send(SCI_STYLESETFORE, SCE_MARKDOWN_CODE, 0xA52A2A);  // Code color

            // Comments (gray) - Note: Markdown may not have comment styling in this version

        } else if (lexerName == "sql") {
            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_SQL_WORD, 0x0000FF);  // Keyword color

            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_SQL_STRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_SQL_COMMENT, 0x808080);  // Comment color
            editor->send(SCI_STYLESETFORE, SCE_SQL_COMMENTLINE, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_SQL_NUMBER, 0xA52A2A);  // Number color

            // Operators
            editor->send(SCI_STYLESETFORE, SCE_SQL_OPERATOR, 0x000000);  // Operator color

            // Set keywords
            const char* sqlKeywords = "SELECT FROM WHERE GROUP BY HAVING ORDER LIMIT OFFSET "
                                      "INSERT INTO UPDATE DELETE CREATE TABLE DROP ALTER "
                                      "INDEX VIEW UNION ALL DISTINCT AS AND OR NOT NULL "
                                      "INNER JOIN LEFT JOIN RIGHT JOIN FULL OUTER JOIN "
                                      "PRIMARY KEY FOREIGN KEY REFERENCES UNIQUE CHECK "
                                      "DEFAULT AUTO_INCREMENT";
            editor->send(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(sqlKeywords));

        } else if (lexerName == "java") {
            // Keywords (blue)
            editor->send(SCI_STYLESETFORE, SCE_C_WORD, 0x0000FF);  // Keyword color

            // Strings (green)
            editor->send(SCI_STYLESETFORE, SCE_C_STRING, 0x008000);  // String color

            // Comments (gray)
            editor->send(SCI_STYLESETFORE, SCE_C_COMMENT, 0x808080);  // Comment color
            editor->send(SCI_STYLESETFORE, SCE_C_COMMENTLINE, 0x808080);  // Comment color

            // Numbers (brown)
            editor->send(SCI_STYLESETFORE, SCE_C_NUMBER, 0xA52A2A);  // Number color

            // Operators
            editor->send(SCI_STYLESETFORE, SCE_C_OPERATOR, 0x000000);  // Operator color

            // Set keywords
            const char* javaKeywords = "abstract assert boolean break byte case catch char class "
                                       "const continue default do double else enum extends final "
                                       "finally float for goto if implements import instanceOf int "
                                       "interface long native new package private protected public "
                                       "return short static strictfp super switch synchronized this "
                                       "throw throws transient try void volatile while";
            editor->send(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>(javaKeywords));

        } else if (lexerName == "null") {
            // Default styling for null lexer
            editor->send(SCI_STYLESETFORE, STYLE_DEFAULT, 0x000000);  // Black text
            editor->send(SCI_STYLESETBACK, STYLE_DEFAULT, 0xFFFFFF);  // White background
        }

        // Force recolorization
        editor->send(SCI_COLOURISE, 0, -1);
    }

signals:
    void titleChanged();

private:
    ScintillaEditBase* editor;
    QSharedPointer<LogicalDocumentState> m_state;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        m_macroManager = new MacroManager(this);
        setupUI();
        setupActions();
        m_panelContentTimer.setSingleShot(true);
        m_panelContentTimer.setInterval(100);
        connect(&m_panelContentTimer, &QTimer::timeout,
                this, &MainWindow::refreshPanelContent);
        // Load session on startup
        loadSession();
        findReplaceDialog = new FindReplaceDialog(this);
        connect(findReplaceDialog, &FindReplaceDialog::findNext, this, &MainWindow::findNext);
        connect(findReplaceDialog, &FindReplaceDialog::findPrevious, this, &MainWindow::findPrevious);
        connect(findReplaceDialog, &FindReplaceDialog::replace, this, &MainWindow::replace);
        connect(findReplaceDialog, &FindReplaceDialog::replaceAll, this, &MainWindow::replaceAll);
        connect(findReplaceDialog, &FindReplaceDialog::closed, this, &MainWindow::findReplaceClosed);

        QSettings settings;
        restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
        restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
        fileBrowserWidget->setRootPath(
            settings.value(QStringLiteral("fileBrowser/root"), QDir::homePath()).toString());
        refreshPanels();
    }
    ~MainWindow() override {
        for (DocumentTab *tab : documentViews()) {
            QObject::disconnect(tab->getEditor(), nullptr, this, nullptr);
            tab->setSessionManager(nullptr);
        }
    }
    bool openPath(const QString &path, QString *error = nullptr);
    bool saveCurrent(QString *error = nullptr);
    bool saveCurrentAs(const QString &path, QString *error = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // File actions
    void newFile();
    void openFile();
    void saveFile();
    void saveAsFile();
    void exitApp();

    // Edit actions
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void find();
    void showReplaceDialog();
    void toggleWordWrap();
    void zoomIn();
    void zoomOut();
    void showAllCharacters();
    void indentGuides();

    // Tab management
    void tabChanged(int index);
    void tabCloseRequested(int index);
    void documentTitleChanged();
    void updateStatusBar();

    // Close actions
    void closeTabAction();
    void closeAllTabsAction();
    void saveAllTabsAction();
    void printFile();
    void functionList();
    void documentMap();
    void fileBrowser();
    void documentList();
    void startMacroRecording();
    void stopMacroRecording();
    void playMacro();
    void runMacroMultipleTimes();
    void saveMacro();
    void syncVertical();
    void syncHorizontal();
    void moveToOtherView();
    void cloneToOtherView();
    void setEncoding(DocumentFormat::TextEncoding encoding);
    void convertEols(DocumentFormat::EolKind eol);

    // Find/Replace functions (restored)
    void findNext();
    void findPrevious();
    void replace();
    void replaceAll();
    void findReplaceClosed();

private:
    void setupUI();
    void setupActions();
    void updateWindowTitle();
    bool loadFile(const QString &filePath, QString *error = nullptr);
    bool saveFileToPath(const QString &filePath, DocumentTab *tab, QString *error = nullptr);
    bool saveTab(DocumentTab *tab, bool forceSaveAs = false);
    void createNewTab(const QString& filePath = "", const QString &documentId = QString(),
                      int restoredUntitledNumber = 0, bool registerWithSession = true);
    void wireDocumentView(DocumentTab *tab);
    void checkpointSessionLayout();
    int lowestAvailableUntitledNumber() const;
    int findTabIndexForFilePath(const QString& filePath);
    bool closeTab(int index, bool ensureOneTab = true, QTabWidget *pane = nullptr);
    bool closeAllTabs();
    DocumentTab* getCurrentTab() const;
    QList<QTabWidget *> panes() const;
    QList<DocumentTab *> documentViews() const;
    QTabWidget *paneFor(DocumentTab *tab) const;
    int viewCount(const QString &documentId) const;
    void updateDualViewActions();
    void updateEditActionsEnabled();
    void refreshPanels();
    void refreshDocumentList();
    void refreshPanelContent();
    void refreshPanelViewport();
    void schedulePanelContentRefresh();
    void navigateToLine(int line);
    void updateMacroActions();
    void rebuildSavedMacroMenu();
    void updateFormatActions();

    // Session management
    CheckpointStatus saveSession();
    void loadSession();

    QTabWidget* tabWidget;
    QTabWidget* secondaryTabWidget;
    QSplitter* editorViewSplitter;
    DualViewManager* dualViewManager;
    QToolBar* toolBar;
    QStatusBar* statusBar;

    // Menu objects
    QMenu *fileMenu;
    QMenu *editMenu;
    QMenu *searchMenu;
    QMenu *viewMenu;
    QMenu *encodingMenu;
    QMenu *languageMenu;
    QMenu *settingsMenu;
    QMenu *toolsMenu;
    QMenu *macroMenu;
    QMenu *savedMacroMenu;
    QMenu *runMenu;
    QMenu *pluginsMenu;
    QMenu *windowMenu;
    QMenu *helpMenu;

    // File menu actions
    QAction *newAction;
    QAction *openAction;
    QAction *saveAction;
    QAction *saveAsAction;
    QAction *exitAction;

    // Edit menu actions
    QAction *undoAction;
    QAction *redoAction;
    QAction *cutAction;
    QAction *copyAction;
    QAction *pasteAction;
    QAction *selectAllAction;

    // Search menu actions
    QAction *findAction;
    QAction *replaceAction;

    // Additional toolbar actions (unimplemented)
    QAction *closeAction;
    QAction *closeAllAction;
    QAction *saveAllAction;
    QAction *printAction;
    QAction *zoomInAction;
    QAction *zoomOutAction;
    QAction *wordWrapAction;
    QAction *showAllCharactersAction;
    QAction *indentGuideAction;
    QAction *functionListAction;
    QAction *documentMapAction;
    QAction *fileBrowserAction;
    QAction *documentListAction;
    QAction *startMacroRecordingAction;
    QAction *stopMacroRecordingAction;
    QAction *playMacroAction;
    QAction *runMacroMultipleTimesAction;
    QAction *saveMacroAction;
    QAction *syncVerticalAction;
    QAction *syncHorizontalAction;
    QAction *moveToOtherViewAction;
    QAction *cloneToOtherViewAction;
    QActionGroup *encodingActionGroup = nullptr;
    QAction *encodingUtf8Action = nullptr;
    QAction *encodingUtf8BomAction = nullptr;
    QAction *encodingUtf16LeAction = nullptr;
    QAction *encodingUtf16BeAction = nullptr;
    QAction *encodingWindows1252Action = nullptr;
    QActionGroup *eolActionGroup = nullptr;
    QAction *eolCrLfAction = nullptr;
    QAction *eolLfAction = nullptr;
    QAction *eolCrAction = nullptr;

    // Find/Replace dialog
    FindReplaceDialog *findReplaceDialog;

    // Session manager
    SessionManager* m_sessionManager;

    // Dock widgets for panels (newly added)
    FunctionList* functionListWidget = nullptr;
    DocumentMap* documentMapWidget = nullptr;
    FileBrowser* fileBrowserWidget = nullptr;
    DocumentList* documentListWidget = nullptr;
    QTimer m_panelContentTimer;
    QString m_sessionDiagnostics;
    QStringList m_documentListIds;
    bool m_loadingSession = false;
    MacroManager *m_macroManager = nullptr;
};

void MainWindow::setupUI() {
    editorViewSplitter = new QSplitter(Qt::Horizontal, this);
    editorViewSplitter->setObjectName(QStringLiteral("editorViewSplitter"));
    secondaryTabWidget = new QTabWidget(editorViewSplitter);
    secondaryTabWidget->setObjectName(QStringLiteral("secondaryTabWidget"));
    tabWidget = new QTabWidget(editorViewSplitter);
    tabWidget->setObjectName(QStringLiteral("primaryTabWidget"));
    editorViewSplitter->insertWidget(0, tabWidget);
    // Enable close buttons on tabs
    tabWidget->setTabsClosable(true);
    secondaryTabWidget->setTabsClosable(true);
    setCentralWidget(editorViewSplitter);
    dualViewManager = new DualViewManager(editorViewSplitter, tabWidget, secondaryTabWidget, this);

    // Set window title
    setWindowTitle("Notepad++");

    // Create status bar
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);

    // Create menu bar
    QMenuBar* menuBar = this->menuBar();

    // File menu
    fileMenu = menuBar->addMenu("&File");

    // Edit menu
    editMenu = menuBar->addMenu("&Edit");

    // Search menu
    searchMenu = menuBar->addMenu("&Search");

    // View menu
    viewMenu = menuBar->addMenu("&View");

    // Encoding menu
    encodingMenu = menuBar->addMenu("&Encoding");

    // Language menu
    languageMenu = menuBar->addMenu("&Language");

    // Settings menu
    settingsMenu = menuBar->addMenu("&Settings");

    // Tools menu
    toolsMenu = menuBar->addMenu("&Tools");

    // Macro menu
    macroMenu = menuBar->addMenu("&Macro");
    macroMenu->setObjectName(QStringLiteral("macroMenu"));
    savedMacroMenu = macroMenu->addMenu("Saved Macros");
    savedMacroMenu->setObjectName(QStringLiteral("savedMacroMenu"));

    // Run menu
    runMenu = menuBar->addMenu("&Run");

    // Plugins menu
    pluginsMenu = menuBar->addMenu("&Plugins");

    // Window menu
    windowMenu = menuBar->addMenu("&Window");

    // Help menu
    helpMenu = menuBar->addMenu("&?");

    // Create toolbar container and store it as member
    toolBar = addToolBar("Main Toolbar");
    toolBar->setObjectName(QStringLiteral("MainToolBar"));
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    functionListWidget = new FunctionList(this);
    documentMapWidget = new DocumentMap(this);
    fileBrowserWidget = new FileBrowser(this);
    documentListWidget = new DocumentList(this);
    addDockWidget(Qt::LeftDockWidgetArea, functionListWidget);
    addDockWidget(Qt::LeftDockWidgetArea, fileBrowserWidget);
    addDockWidget(Qt::RightDockWidgetArea, documentMapWidget);
    addDockWidget(Qt::RightDockWidgetArea, documentListWidget);
    functionListWidget->hide();
    documentMapWidget->hide();
    fileBrowserWidget->hide();
    documentListWidget->hide();

    connect(documentListWidget, &DocumentList::documentActivated, this, [this](int index) {
        if (index < 0 || index >= m_documentListIds.size()) return;
        const QString id = m_documentListIds[index];
        QTabWidget *preferred = dualViewManager->activePane();
        for (QTabWidget *pane : {preferred, dualViewManager->otherPane(preferred)})
            for (int i = 0; i < pane->count(); ++i)
                if (auto *tab = qobject_cast<DocumentTab *>(pane->widget(i)); tab && tab->documentId() == id) {
                    dualViewManager->activate(pane, i); return;
                }
    });
    connect(functionListWidget, &FunctionList::lineActivated,
            this, &MainWindow::navigateToLine);
    connect(documentMapWidget, &DocumentMap::lineActivated,
            this, &MainWindow::navigateToLine);
    connect(fileBrowserWidget, &FileBrowser::fileActivated, this, [this](const QString &path) {
        const int existing = findTabIndexForFilePath(path);
        if (existing < 0)
            createNewTab(path);
    });
}

void MainWindow::setupActions() {
    // File actions
    newAction = new QAction("&New", this);
    newAction->setObjectName(QStringLiteral("newAction"));
    openAction = new QAction("&Open", this);
    saveAction = new QAction("&Save", this);
    saveAsAction = new QAction("Save &As", this);
    exitAction = new QAction("&Exit", this);

    // Edit actions
    undoAction = new QAction("&Undo", this);
    redoAction = new QAction("&Redo", this);
    cutAction = new QAction("Cu&t", this);
    copyAction = new QAction("&Copy", this);
    pasteAction = new QAction("&Paste", this);
    selectAllAction = new QAction("Select &All", this);

    // Search actions
    findAction = new QAction("&Find", this);
    replaceAction = new QAction("&Replace", this);

    // Additional toolbar actions (unimplemented)
    closeAction = new QAction("Close", this);
    closeAllAction = new QAction("Close All", this);
    saveAllAction = new QAction("Save All", this);
    printAction = new QAction("Print", this);
    zoomInAction = new QAction("Zoom In", this);
    zoomOutAction = new QAction("Zoom Out", this);
    wordWrapAction = new QAction("Word Wrap", this);
    showAllCharactersAction = new QAction("Show All Characters", this);
    indentGuideAction = new QAction("Indent Guide", this);
    functionListAction = new QAction("Function List", this);
    documentMapAction = new QAction("Document Map", this);
    fileBrowserAction = new QAction("File Browser", this);
    documentListAction = new QAction("Document List", this);
    startMacroRecordingAction = new QAction("Start Macro Recording", this);
    startMacroRecordingAction->setObjectName(QStringLiteral("macroStartAction"));
    stopMacroRecordingAction = new QAction("Stop Macro Recording", this);
    stopMacroRecordingAction->setObjectName(QStringLiteral("macroStopAction"));
    playMacroAction = new QAction("Play Macro", this);
    playMacroAction->setObjectName(QStringLiteral("macroPlayAction"));
    runMacroMultipleTimesAction = new QAction("Run Macro Multiple Times", this);
    runMacroMultipleTimesAction->setObjectName(QStringLiteral("macroRepeatAction"));
    saveMacroAction = new QAction("Save Macro", this);
    saveMacroAction->setObjectName(QStringLiteral("macroSaveAction"));
    syncVerticalAction = new QAction("Sync Vertical", this);
    syncHorizontalAction = new QAction("Sync Horizontal", this);
    moveToOtherViewAction = new QAction(tr("Move to Other View"), this);
    moveToOtherViewAction->setObjectName(QStringLiteral("moveToOtherViewAction"));
    cloneToOtherViewAction = new QAction(tr("Clone to Other View"), this);
    cloneToOtherViewAction->setObjectName(QStringLiteral("cloneToOtherViewAction"));

    encodingActionGroup = new QActionGroup(this);
    encodingActionGroup->setExclusive(true);
    auto addEncodingAction = [this](const QString &text, const QString &name,
                                    DocumentFormat::TextEncoding encoding) {
        QAction *item = encodingMenu->addAction(text);
        item->setObjectName(name);
        item->setCheckable(true);
        encodingActionGroup->addAction(item);
        connect(item, &QAction::triggered, this, [this, encoding] { setEncoding(encoding); });
        return item;
    };
    encodingUtf8Action = addEncodingAction(tr("UTF-8"), QStringLiteral("encodingUtf8Action"),
                                           DocumentFormat::TextEncoding::Utf8);
    encodingUtf8BomAction = addEncodingAction(tr("UTF-8 BOM"), QStringLiteral("encodingUtf8BomAction"),
                                              DocumentFormat::TextEncoding::Utf8Bom);
    encodingUtf16LeAction = addEncodingAction(tr("UTF-16 LE"), QStringLiteral("encodingUtf16LeAction"),
                                              DocumentFormat::TextEncoding::Utf16Le);
    encodingUtf16BeAction = addEncodingAction(tr("UTF-16 BE"), QStringLiteral("encodingUtf16BeAction"),
                                              DocumentFormat::TextEncoding::Utf16Be);
    encodingWindows1252Action = addEncodingAction(tr("Windows-1252"), QStringLiteral("encodingWindows1252Action"),
                                                  DocumentFormat::TextEncoding::Windows1252);

    QMenu *eolMenu = editMenu->addMenu(tr("EOL Conversion"));
    eolMenu->setObjectName(QStringLiteral("eolConversionMenu"));
    eolActionGroup = new QActionGroup(this);
    eolActionGroup->setExclusive(true);
    auto addEolAction = [this, eolMenu](const QString &text, const QString &name,
                                       DocumentFormat::EolKind eol) {
        QAction *item = eolMenu->addAction(text);
        item->setObjectName(name);
        item->setCheckable(true);
        eolActionGroup->addAction(item);
        connect(item, &QAction::triggered, this, [this, eol] { convertEols(eol); });
        return item;
    };
    eolCrLfAction = addEolAction(tr("Windows (CR LF)"), QStringLiteral("eolCrLfAction"),
                                 DocumentFormat::EolKind::CrLf);
    eolLfAction = addEolAction(tr("Unix (LF)"), QStringLiteral("eolLfAction"),
                               DocumentFormat::EolKind::Lf);
    eolCrAction = addEolAction(tr("Macintosh (CR)"), QStringLiteral("eolCrAction"),
                               DocumentFormat::EolKind::Cr);
    syncVerticalAction->setObjectName(QStringLiteral("synchronizeVerticalScrollingAction"));
    syncHorizontalAction->setObjectName(QStringLiteral("synchronizeHorizontalScrollingAction"));
    syncVerticalAction->setText(tr("Synchronize Vertical Scrolling"));
    syncHorizontalAction->setText(tr("Synchronize Horizontal Scrolling"));

    // Connect file actions
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveFile);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAsFile);
    connect(exitAction, &QAction::triggered, this, &MainWindow::exitApp);

    // Connect edit actions
    connect(undoAction, &QAction::triggered, this, &MainWindow::undo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::redo);
    connect(cutAction, &QAction::triggered, this, &MainWindow::cut);
    connect(copyAction, &QAction::triggered, this, &MainWindow::copy);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::paste);
    connect(selectAllAction, &QAction::triggered, this, &MainWindow::selectAll);

    // Connect search actions
    connect(findAction, &QAction::triggered, this, &MainWindow::find);
    connect(replaceAction, &QAction::triggered, this, &MainWindow::showReplaceDialog);

    // Connect tab management actions
    connect(closeAction, &QAction::triggered, this, &MainWindow::closeTabAction);
    connect(closeAllAction, &QAction::triggered, this, &MainWindow::closeAllTabsAction);
    connect(saveAllAction, &QAction::triggered, this, &MainWindow::saveAllTabsAction);

    // Connect additional toolbar actions (unimplemented)
    connect(printAction, &QAction::triggered, this, &MainWindow::printFile);
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    connect(wordWrapAction, &QAction::triggered, this, &MainWindow::toggleWordWrap);
    connect(showAllCharactersAction, &QAction::triggered, this, &MainWindow::showAllCharacters);
    connect(indentGuideAction, &QAction::triggered, this, &MainWindow::indentGuides);
    connect(functionListAction, &QAction::triggered, this, &MainWindow::functionList);
    connect(documentMapAction, &QAction::triggered, this, &MainWindow::documentMap);
    connect(fileBrowserAction, &QAction::triggered, this, &MainWindow::fileBrowser);
    connect(documentListAction, &QAction::triggered, this, &MainWindow::documentList);
    connect(startMacroRecordingAction, &QAction::triggered, this, &MainWindow::startMacroRecording);
    connect(stopMacroRecordingAction, &QAction::triggered, this, &MainWindow::stopMacroRecording);
    connect(playMacroAction, &QAction::triggered, this, &MainWindow::playMacro);
    connect(runMacroMultipleTimesAction, &QAction::triggered, this, &MainWindow::runMacroMultipleTimes);
    connect(saveMacroAction, &QAction::triggered, this, &MainWindow::saveMacro);
    connect(m_macroManager, &MacroManager::stateChanged,
            this, &MainWindow::updateMacroActions);
    connect(m_macroManager, &MacroManager::savedMacrosChanged,
            this, &MainWindow::rebuildSavedMacroMenu);
    connect(syncVerticalAction, &QAction::triggered, this, &MainWindow::syncVertical);
    connect(syncHorizontalAction, &QAction::triggered, this, &MainWindow::syncHorizontal);
    connect(moveToOtherViewAction, &QAction::triggered, this, &MainWindow::moveToOtherView);
    connect(cloneToOtherViewAction, &QAction::triggered, this, &MainWindow::cloneToOtherView);

    // Connect tab signals
    connect(tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0) dualViewManager->activate(tabWidget, index);
        dualViewManager->recaptureSyncOffsets();
        tabChanged(index);
    });
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::tabCloseRequested);
    connect(secondaryTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index >= 0) dualViewManager->activate(secondaryTabWidget, index);
        dualViewManager->recaptureSyncOffsets();
        tabChanged(index);
    });
    connect(secondaryTabWidget, &QTabWidget::tabCloseRequested, this,
            [this](int index) { closeTab(index, true, secondaryTabWidget); });
    connect(dualViewManager, &DualViewManager::activePaneChanged, this, [this] { tabChanged(0); });

    // Add actions to menus
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addAction(saveAllAction);  // Add Save All to File menu
    fileMenu->addAction(printAction);
    fileMenu->addSeparator();
    fileMenu->addAction(closeAction);    // Add Close to File menu
    fileMenu->addAction(closeAllAction); // Add Close All to File menu
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(cutAction);
    editMenu->addAction(copyAction);
    editMenu->addAction(pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(selectAllAction);

    searchMenu->addAction(findAction);
    searchMenu->addAction(replaceAction);
    viewMenu->addAction(moveToOtherViewAction);
    viewMenu->addAction(cloneToOtherViewAction);
    viewMenu->addAction(syncVerticalAction);
    viewMenu->addAction(syncHorizontalAction);
    for (QTabWidget *pane : {tabWidget, secondaryTabWidget}) {
        pane->setContextMenuPolicy(Qt::ActionsContextMenu);
        pane->addAction(moveToOtherViewAction);
        pane->addAction(cloneToOtherViewAction);
        pane->addAction(syncVerticalAction);
        pane->addAction(syncHorizontalAction);
    }

    macroMenu->insertAction(savedMacroMenu->menuAction(), startMacroRecordingAction);
    macroMenu->insertAction(savedMacroMenu->menuAction(), stopMacroRecordingAction);
    macroMenu->insertSeparator(savedMacroMenu->menuAction());
    macroMenu->insertAction(savedMacroMenu->menuAction(), playMacroAction);
    macroMenu->insertAction(savedMacroMenu->menuAction(), runMacroMultipleTimesAction);
    macroMenu->insertAction(savedMacroMenu->menuAction(), saveMacroAction);
    macroMenu->insertSeparator(savedMacroMenu->menuAction());

    // Add actions to toolbar - use the member variable instead of findChild
    toolBar->addAction(newAction);
    toolBar->addAction(openAction);
    toolBar->addAction(saveAction);
    toolBar->addSeparator();
    toolBar->addAction(undoAction);
    toolBar->addAction(redoAction);
    toolBar->addSeparator();
    toolBar->addAction(cutAction);
    toolBar->addAction(copyAction);
    toolBar->addAction(pasteAction);
    toolBar->addSeparator();
    toolBar->addAction(findAction);
    toolBar->addAction(replaceAction);
    toolBar->addSeparator();

    // Add additional toolbar actions
    toolBar->addAction(closeAction);
    toolBar->addAction(closeAllAction);
    toolBar->addAction(saveAllAction);
    toolBar->addAction(printAction);
    toolBar->addSeparator();
    toolBar->addAction(wordWrapAction);
    toolBar->addSeparator();
    toolBar->addAction(zoomInAction);
    toolBar->addAction(zoomOutAction);
    toolBar->addSeparator();
    toolBar->addAction(showAllCharactersAction);
    toolBar->addSeparator();
    toolBar->addAction(indentGuideAction);
    toolBar->addSeparator();
    toolBar->addAction(functionListAction);
    toolBar->addAction(documentMapAction);
    toolBar->addAction(fileBrowserAction);
    toolBar->addAction(documentListAction);
    toolBar->addSeparator();
    toolBar->addAction(startMacroRecordingAction);
    toolBar->addAction(stopMacroRecordingAction);
    toolBar->addAction(playMacroAction);
    toolBar->addAction(runMacroMultipleTimesAction);
    toolBar->addAction(saveMacroAction);
    toolBar->addSeparator();
    toolBar->addAction(syncVerticalAction);
    toolBar->addAction(syncHorizontalAction);
    toolBar->addAction(moveToOtherViewAction);
    toolBar->addAction(cloneToOtherViewAction);

    // Set shortcuts
    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    exitAction->setShortcut(QKeySequence::Quit);
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);
    cutAction->setShortcut(QKeySequence::Cut);
    copyAction->setShortcut(QKeySequence::Copy);
    pasteAction->setShortcut(QKeySequence::Paste);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    findAction->setShortcut(QKeySequence::Find);
    replaceAction->setShortcut(QKeySequence("Ctrl+H"));
    zoomInAction->setShortcut(QKeySequence("Ctrl++"));
    zoomOutAction->setShortcut(QKeySequence("Ctrl+-"));

    // Set tooltips for unimplemented actions
    closeAction->setToolTip("Close");
    closeAllAction->setToolTip("Close All");
    saveAllAction->setToolTip("Save All");
    printAction->setToolTip("Print");
    printAction->setShortcut(QKeySequence::Print);
    zoomInAction->setToolTip("Zoom In");
    zoomOutAction->setToolTip("Zoom Out");
    wordWrapAction->setToolTip("Word Wrap");
    showAllCharactersAction->setToolTip("Show All Characters");
    indentGuideAction->setToolTip("Indent Guide");
    functionListAction->setToolTip("Function List");
    documentMapAction->setToolTip("Document Map");
    fileBrowserAction->setToolTip("File Browser");
    documentListAction->setToolTip("Document List");
    startMacroRecordingAction->setToolTip("Start Macro Recording");
    stopMacroRecordingAction->setToolTip("Stop Macro Recording");
    playMacroAction->setToolTip("Play Macro");
    runMacroMultipleTimesAction->setToolTip("Run Macro Multiple Times");
    saveMacroAction->setToolTip("Save Macro");
    syncVerticalAction->setToolTip("Sync Vertical");
    syncHorizontalAction->setToolTip("Sync Horizontal");

    // Enable the actions that were previously disabled
    closeAction->setEnabled(true);
    closeAllAction->setEnabled(true);
    saveAllAction->setEnabled(true);
    wordWrapAction->setEnabled(true);
    wordWrapAction->setCheckable(true);
    zoomInAction->setEnabled(true);
    zoomOutAction->setEnabled(true);
    showAllCharactersAction->setEnabled(true);
    showAllCharactersAction->setCheckable(true);
    indentGuideAction->setEnabled(true);
    indentGuideAction->setCheckable(true);

    // Disable unimplemented actions
    printAction->setEnabled(true);
    for (QAction *panelAction : {functionListAction, documentMapAction,
                                 fileBrowserAction, documentListAction}) {
        panelAction->setEnabled(true);
        panelAction->setCheckable(true);
        viewMenu->addAction(panelAction);
    }
    connect(functionListWidget, &QDockWidget::visibilityChanged,
            functionListAction, &QAction::setChecked);
    connect(documentMapWidget, &QDockWidget::visibilityChanged,
            documentMapAction, &QAction::setChecked);
    connect(fileBrowserWidget, &QDockWidget::visibilityChanged,
            fileBrowserAction, &QAction::setChecked);
    connect(documentListWidget, &QDockWidget::visibilityChanged,
            documentListAction, &QAction::setChecked);
    rebuildSavedMacroMenu();
    updateMacroActions();
    syncVerticalAction->setCheckable(true);
    syncHorizontalAction->setCheckable(true);
    dualViewManager->bindSyncActions(syncVerticalAction, syncHorizontalAction);
    updateDualViewActions();

    // Assign icons to actions
    newAction->setIcon(QIcon(":/icons/new.ico"));
    openAction->setIcon(QIcon(":/icons/open.ico"));
    saveAction->setIcon(QIcon(":/icons/save.ico"));
    undoAction->setIcon(QIcon(":/icons/undo.ico"));
    redoAction->setIcon(QIcon(":/icons/redo.ico"));
    cutAction->setIcon(QIcon(":/icons/cut.ico"));
    copyAction->setIcon(QIcon(":/icons/copy.ico"));
    pasteAction->setIcon(QIcon(":/icons/paste.ico"));
    findAction->setIcon(QIcon(":/icons/find.ico"));
    replaceAction->setIcon(QIcon(":/icons/replace.ico"));

    // Assign icons to unimplemented actions
    closeAction->setIcon(QIcon(":/icons/close.ico"));
    closeAllAction->setIcon(QIcon(":/icons/closeall.ico"));
    saveAllAction->setIcon(QIcon(":/icons/saveall.ico"));
    printAction->setIcon(QIcon(":/icons/print.ico"));
    zoomInAction->setIcon(QIcon(":/icons/zoomin.ico"));
    zoomOutAction->setIcon(QIcon(":/icons/zoomout.ico"));
    wordWrapAction->setIcon(QIcon(":/icons/wrap.ico"));
    showAllCharactersAction->setIcon(QIcon(":/icons/allchars.ico"));
    indentGuideAction->setIcon(QIcon(":/icons/indentguide.ico"));
    functionListAction->setIcon(QIcon(":/icons/funclist.ico"));
    documentMapAction->setIcon(QIcon(":/icons/docmap.ico"));
    fileBrowserAction->setIcon(QIcon(":/icons/filebrowser.ico"));
    documentListAction->setIcon(QIcon(":/icons/doclist.ico"));
    startMacroRecordingAction->setIcon(QIcon(":/icons/record.ico"));
    stopMacroRecordingAction->setIcon(QIcon(":/icons/stoprecord.ico"));
    playMacroAction->setIcon(QIcon(":/icons/playrecord.ico"));
    runMacroMultipleTimesAction->setIcon(QIcon(":/icons/playrecordmulti.ico"));
    saveMacroAction->setIcon(QIcon(":/icons/saverecord.ico"));
    syncVerticalAction->setIcon(QIcon(":/icons/syncv.ico"));
    syncHorizontalAction->setIcon(QIcon(":/icons/synch.ico"));
}

void MainWindow::newFile() {
    createNewTab();
}

void MainWindow::openFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open File", "", "All Files (*)");
    if (!fileName.isEmpty()) {
        // Check if file is already open
        int existingIndex = findTabIndexForFilePath(fileName);
        if (existingIndex != -1) {
            return;
        }

        createNewTab(fileName);
    }
}

void MainWindow::saveFile() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab)
        saveTab(currentTab);
}

void MainWindow::saveAsFile() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    saveTab(currentTab, true);
}

void MainWindow::exitApp() {
    close();
}

void MainWindow::undo() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_UNDO);
    }
}

void MainWindow::redo() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_REDO);
    }
}

void MainWindow::cut() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_CUT);
    }
}

void MainWindow::copy() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_COPY);
    }
}

void MainWindow::paste() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_PASTE);
    }
}

void MainWindow::selectAll() {
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_SELECTALL);
    }
}

void MainWindow::find() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    // Show find dialog
    findReplaceDialog->showFind();
    findReplaceDialog->setFindText("");
    findReplaceDialog->show();
    findReplaceDialog->raise();
    findReplaceDialog->activateWindow();
}

void MainWindow::showReplaceDialog() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    // Show replace dialog
    findReplaceDialog->showReplace();
    findReplaceDialog->setFindText("");
    findReplaceDialog->show();
    findReplaceDialog->raise();
    findReplaceDialog->activateWindow();
}

void MainWindow::toggleWordWrap() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();

    // Get current wrap mode
    int currentWrapMode = editor->send(SCI_GETWRAPMODE);

    // Toggle wrap mode
    int newWrapMode = (currentWrapMode == SC_WRAP_WORD) ? SC_WRAP_NONE : SC_WRAP_WORD;

    // Apply the new wrap mode
    editor->send(SCI_SETWRAPMODE, newWrapMode);

    // Update the action's checked state
    wordWrapAction->setChecked(newWrapMode == SC_WRAP_WORD);
}

void MainWindow::zoomIn() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();

    // Zoom in using Scintilla's native zoom functionality
    editor->send(SCI_ZOOMIN);
}

void MainWindow::zoomOut() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();

    // Zoom out using Scintilla's native zoom functionality
    editor->send(SCI_ZOOMOUT);
}

void MainWindow::showAllCharacters() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();

    // Get current state
    int currentViewWS = editor->send(SCI_GETVIEWWS);
    bool isCurrentlyVisible = (currentViewWS == SCWS_VISIBLEALWAYS);

    // Toggle the visibility
    int newViewWS = isCurrentlyVisible ? SCWS_INVISIBLE : SCWS_VISIBLEALWAYS;

    // Apply the new setting
    editor->send(SCI_SETVIEWWS, newViewWS);

    // Also toggle EOL visibility
    bool currentViewEOL = editor->send(SCI_GETVIEWEOL);
    int newViewEOL = currentViewEOL ? 0 : 1;
    editor->send(SCI_SETVIEWEOL, newViewEOL);

    // Update the action's checked state
    showAllCharactersAction->setChecked(!isCurrentlyVisible);
}

void MainWindow::indentGuides() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();

    // Get current state
    int currentIndentGuides = editor->send(SCI_GETINDENTATIONGUIDES);
    bool isCurrentlyVisible = (currentIndentGuides == SC_IV_LOOKFORWARD);

    // Toggle the visibility
    int newIndentGuides = isCurrentlyVisible ? SC_IV_NONE : SC_IV_LOOKFORWARD;

    // Apply the new setting
    editor->send(SCI_SETINDENTATIONGUIDES, newIndentGuides);

    // Update the action's checked state
    indentGuideAction->setChecked(!isCurrentlyVisible);
}

void MainWindow::tabChanged(int index) {
    Q_UNUSED(index)
    checkpointSessionLayout();
    updateEditActionsEnabled();
    updateStatusBar();
    refreshPanels();
    updateMacroActions();
    updateDualViewActions();
    updateFormatActions();

    // Update word wrap action state to match current tab
    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        ScintillaEditBase* editor = currentTab->getEditor();
        int wrapMode = editor->send(SCI_GETWRAPMODE);
        wordWrapAction->setChecked(wrapMode == SC_WRAP_WORD);

        // Update show all characters action state to match current tab
        int viewWS = editor->send(SCI_GETVIEWWS);
        bool isWSVisible = (viewWS == SCWS_VISIBLEALWAYS);
        bool viewEOL = editor->send(SCI_GETVIEWEOL);
        showAllCharactersAction->setChecked(isWSVisible || viewEOL);

        // Update indent guide action state to match current tab
        int indentGuides = editor->send(SCI_GETINDENTATIONGUIDES);
        bool isIndentGuideVisible = (indentGuides == SC_IV_LOOKFORWARD);
        indentGuideAction->setChecked(isIndentGuideVisible);
    }
}

void MainWindow::tabCloseRequested(int index) {
    closeTab(index, true, tabWidget);
}

void MainWindow::documentTitleChanged() {
    DocumentTab* tab = qobject_cast<DocumentTab*>(sender());
    if (tab) {
        for (DocumentTab *view : documentViews()) {
          if (view->documentId() == tab->documentId()) {
            QTabWidget *pane = paneFor(view);
            int index = pane->indexOf(view);
            QString title;
            if (tab->getFilePath().isEmpty()) {
                // For untitled documents, use the tab number that was assigned
                title = QString("new %1").arg(tab->getTabNumber());
            } else {
                title = QFileInfo(tab->getFilePath()).fileName();
            }

            if (tab->isDirty()) {
                title += "*";
            }
            pane->setTabText(index, title);
          }
        }
    }
    updateStatusBar();
    refreshDocumentList();
}

void MainWindow::updateStatusBar() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) {
        statusBar->clearMessage();
        return;
    }

    ScintillaEditBase* editor = currentTab->getEditor();

    // Get caret position
    int line = editor->send(SCI_LINEFROMPOSITION, editor->send(SCI_GETCURRENTPOS));
    int col = editor->send(SCI_GETCOLUMN, editor->send(SCI_GETCURRENTPOS));

    // Get selection length
    Scintilla::Position anchor = editor->send(SCI_GETANCHOR);
    Scintilla::Position currentPos = editor->send(SCI_GETCURRENTPOS);
    int selLength = abs(static_cast<int>(currentPos - anchor));

    // Get total line count
    int lineCount = editor->send(SCI_GETLINECOUNT);

    const QString eolStr = DocumentFormat::eolName(currentTab->eol());

    // Get insert/overwrite mode
    bool overwrite = editor->send(SCI_GETOVERTYPE);
    QString modeStr = overwrite ? "OVR" : "INS";

    // Format status bar text
    QString statusText = QString("Ln %1, Col %2    Sel %3    Lines %4    %5    %6    %7")
                         .arg(line + 1)
                         .arg(col + 1)
                         .arg(selLength)
                         .arg(lineCount)
                         .arg(eolStr)
                         .arg(DocumentFormat::encodingName(currentTab->encoding()))
                         .arg(modeStr);

    if (!currentTab->recoveryWarning().isEmpty())
        statusText += QStringLiteral("    WARNING: %1").arg(currentTab->recoveryWarning());
    if (!m_sessionDiagnostics.isEmpty())
        statusText += QStringLiteral("    RECOVERY: %1").arg(m_sessionDiagnostics);
    statusBar->showMessage(statusText);
}

void MainWindow::createNewTab(const QString& filePath, const QString &documentId,
                              int restoredUntitledNumber, bool registerWithSession) {
    const int assignedNumber = restoredUntitledNumber > 0
                                   ? restoredUntitledNumber
                                   : lowestAvailableUntitledNumber();
    DocumentTab* newTab = new DocumentTab(filePath, assignedNumber, documentId, this);

    wireDocumentView(newTab);

    QString title;
    if (filePath.isEmpty()) {
        title = QString("new %1").arg(assignedNumber);
    } else {
        // Interactive open suppresses duplicates; identity-based restoration does not.
        if (registerWithSession) {
            int existingIndex = findTabIndexForFilePath(filePath);
            if (existingIndex != -1) {
                newTab->deleteLater();
                return;
            }
        }
        title = QFileInfo(filePath).fileName();
    }

    QTabWidget *targetPane = m_loadingSession ? tabWidget : dualViewManager->activePane();
    int index = targetPane->addTab(newTab, title);
    dualViewManager->activate(targetPane, index);

    if (!filePath.isEmpty() && registerWithSession && !loadFile(filePath)) {
        targetPane->removeTab(index);
        newTab->deleteLater();
        return;
    }

    if (registerWithSession) {
        newTab->setSessionManager(m_sessionManager);
        newTab->checkpoint();
        checkpointSessionLayout();
    }

    // Update tab text to include asterisk if needed
    documentTitleChanged();

    // Update status bar for new tab
    updateStatusBar();
    dualViewManager->updatePaneVisibility();
    updateDualViewActions();
    updateFormatActions();
}

void MainWindow::wireDocumentView(DocumentTab *tab)
{
    connect(tab, &DocumentTab::titleChanged, this, &MainWindow::documentTitleChanged);
    connect(tab->getEditor(), &ScintillaEditBase::notify, this, &MainWindow::updateStatusBar);
    connect(tab->getEditor(), &ScintillaEditBase::notify, this,
            [this, tab](Scintilla::NotificationData *notification) {
                if (tab != getCurrentTab()) return;
                if (notification->nmhdr.code == Scintilla::Notification::Modified &&
                    (Scintilla::FlagSet(notification->modificationType,
                                        Scintilla::ModificationFlags::InsertText) ||
                     Scintilla::FlagSet(notification->modificationType,
                                        Scintilla::ModificationFlags::DeleteText)))
                    schedulePanelContentRefresh();
            });
    connect(tab->getEditor(), &ScintillaEditBase::verticalScrolled, this,
            [this, tab](int) {
                if (tab == getCurrentTab()) refreshPanelViewport();
            });
    dualViewManager->connectEditor(tab->getEditor());
}

int MainWindow::lowestAvailableUntitledNumber() const
{
    QSet<int> usedNumbers;
    for (auto *tab : documentViews()) {
        if (tab && tab->getFilePath().isEmpty() && tab->getTabNumber() > 0)
            usedNumbers.insert(tab->getTabNumber());
    }
    int candidate = 1;
    while (usedNumbers.contains(candidate))
        ++candidate;
    return candidate;
}

void MainWindow::checkpointSessionLayout()
{
    if (!m_sessionManager || m_loadingSession)
        return;
    QStringList ids;
    QSet<QString> seen;
    for (auto *tab : documentViews()) {
        if (tab && tab->isDisposablePlaceholder()) {
            m_sessionManager->removeDocument(tab->documentId());
        } else if (tab && !seen.contains(tab->documentId())) {
            ids << tab->documentId();
            seen.insert(tab->documentId());
        }
    }
    DocumentTab *active = getCurrentTab();
    const QString activeId = active && !active->isDisposablePlaceholder()
                                 ? active->documentId()
                                 : QString();
    m_sessionManager->setSessionLayout(ids, activeId);
    QStringList primaryIds, secondaryIds;
    for (int i = 0; i < tabWidget->count(); ++i)
        if (auto *tab = qobject_cast<DocumentTab *>(tabWidget->widget(i)); tab && !tab->isDisposablePlaceholder())
            primaryIds << tab->documentId();
    for (int i = 0; i < secondaryTabWidget->count(); ++i)
        if (auto *tab = qobject_cast<DocumentTab *>(secondaryTabWidget->widget(i)); tab && !tab->isDisposablePlaceholder())
            secondaryIds << tab->documentId();
    m_sessionManager->setDualViewLayout(primaryIds, secondaryIds,
        dualViewManager->activePane() == secondaryTabWidget ? QStringLiteral("secondary") : QStringLiteral("primary"),
        editorViewSplitter->orientation(), editorViewSplitter->sizes());
}

int MainWindow::findTabIndexForFilePath(const QString& filePath) {
    QTabWidget *active = dualViewManager->activePane();
    for (QTabWidget *pane : {active, dualViewManager->otherPane(active)}) {
      for (int i = 0; i < pane->count(); ++i) {
        DocumentTab* tab = qobject_cast<DocumentTab*>(pane->widget(i));
        if (tab && tab->getFilePath() == filePath) {
            dualViewManager->activate(pane, i);
            return i;
        }
      }
    }
    return -1;
}

bool MainWindow::closeTab(int index, bool ensureOneTab, QTabWidget *pane) {
    pane = pane ? pane : dualViewManager->activePane();
    DocumentTab* tab = qobject_cast<DocumentTab*>(pane->widget(index));
    if (!tab) return false;
    const bool finalView = viewCount(tab->documentId()) == 1;

    // If the tab is modified, ask user
    if (finalView && tab->isDirty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Save Changes");
        msgBox.setText("The document has been modified.");
        msgBox.setInformativeText("Do you want to save your changes?");
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);

        int ret = msgBox.exec();
        switch (ret) {
            case QMessageBox::Save:
                if (!saveTab(tab)) {
                    return false; // Save failed, cancel closing
                }
                break;
            case QMessageBox::Discard:
                break; // Just close
            case QMessageBox::Cancel:
                return false; // Cancel closing
        }
    }

    if (finalView && m_sessionManager)
        m_sessionManager->removeDocument(tab->documentId());
    pane->removeTab(index);
    tab->deleteLater();
    dualViewManager->updatePaneVisibility();
    updateDualViewActions();

    // Ensure at least one tab remains
    if (ensureOneTab && tabWidget->count() + secondaryTabWidget->count() == 0) {
        createNewTab();
    }
    checkpointSessionLayout();

    return true;
}

bool MainWindow::closeAllTabs() {
    for (QTabWidget *pane : {secondaryTabWidget, tabWidget}) {
      for (int i = pane->count() - 1; i >= 0; --i) {
        if (!closeTab(i, false, pane)) {
            return false; // Cancelled by user
        }
      }
    }
    return true;
}

DocumentTab* MainWindow::getCurrentTab() const {
    QWidget* currentWidget = dualViewManager->activePane()->currentWidget();
    return qobject_cast<DocumentTab*>(currentWidget);
}

QList<QTabWidget *> MainWindow::panes() const { return {tabWidget, secondaryTabWidget}; }
QList<DocumentTab *> MainWindow::documentViews() const {
    QList<DocumentTab *> result;
    for (QTabWidget *pane : panes()) for (int i = 0; i < pane->count(); ++i)
        if (auto *tab = qobject_cast<DocumentTab *>(pane->widget(i))) result << tab;
    return result;
}
QTabWidget *MainWindow::paneFor(DocumentTab *tab) const {
    for (QTabWidget *pane : panes()) if (pane->indexOf(tab) >= 0) return pane;
    return nullptr;
}
int MainWindow::viewCount(const QString &id) const {
    int count = 0; for (auto *tab : documentViews()) if (tab->documentId() == id) ++count;
    return count;
}

void MainWindow::updateEditActionsEnabled() {
    DocumentTab* currentTab = getCurrentTab();
    bool hasEditor = (currentTab != nullptr);

    undoAction->setEnabled(hasEditor && currentTab->getEditor()->send(SCI_CANUNDO));
    redoAction->setEnabled(hasEditor && currentTab->getEditor()->send(SCI_CANREDO));
    cutAction->setEnabled(hasEditor);
    copyAction->setEnabled(hasEditor);
    pasteAction->setEnabled(hasEditor);
    selectAllAction->setEnabled(hasEditor);
}

bool MainWindow::loadFile(const QString &filePath, QString *error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        else QMessageBox::warning(this, "Error", "Could not open file for reading.");
        return false;
    }

    const QByteArray content = file.readAll();
    file.close();
    const DocumentFormat::DecodedDocument decoded = DocumentFormat::decode(content);
    if (!decoded.success) {
        if (error) *error = decoded.error;
        else QMessageBox::warning(this, tr("Error"), decoded.error);
        return false;
    }

    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_CLEARALL);
        currentTab->getEditor()->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(decoded.utf8.constData()));
        currentTab->setDocumentFormat(decoded.encoding, decoded.eol.kind, decoded.eol.insertion);
        currentTab->getEditor()->send(SCI_SETSAVEPOINT);
        currentTab->setFilePath(filePath);
        currentTab->setDirty(false);
    }

    // Update status bar after loading file
    updateStatusBar();

    return true;
}

bool MainWindow::saveFileToPath(const QString &filePath, DocumentTab* tab, QString *errorOut) {
    if (!tab) return false;

    QString error;
    if (!DocumentFormat::saveAtomic(filePath, EditorUtils::text(tab->getEditor()),
                                    tab->encoding(), &error)) {
        if (errorOut) *errorOut = error;
        else QMessageBox::warning(this, tr("Error"),
                                  tr("Could not save %1: %2").arg(filePath, error));
        return false;
    }
    tab->markExplicitlySaved();
    tab->getEditor()->send(SCI_SETSAVEPOINT);
    tab->setDirty(false);
    return true;
}

bool MainWindow::saveTab(DocumentTab *tab, bool forceSaveAs)
{
    if (!tab)
        return false;
    QString destination = tab->getFilePath();
    if (forceSaveAs || destination.isEmpty()) {
        destination = QFileDialog::getSaveFileName(
            this, tr("Save File"), destination, tr("All Files (*)"));
        if (destination.isEmpty())
            return false;
    }
    if (!saveFileToPath(destination, tab))
        return false;
    tab->setFilePath(destination);
    tab->setRecoveryWarning({});
    tab->setRecoveryCheckpointBlocked(false);
    tab->checkpoint();
    checkpointSessionLayout();
    refreshPanels();
    return true;
}

bool MainWindow::openPath(const QString &path, QString *error)
{
    if (path.isEmpty()) {
        if (error) *error = tr("No source path");
        return false;
    }
    if (findTabIndexForFilePath(path) >= 0)
        return true;
    QTabWidget *target = dualViewManager->activePane();
    const int previousCount = target->count();
    createNewTab(path);
    if (target->count() == previousCount) {
        if (error) *error = tr("Could not open file");
        return false;
    }
    return true;
}

bool MainWindow::saveCurrent(QString *error)
{
    DocumentTab *tab = getCurrentTab();
    if (!tab || tab->getFilePath().isEmpty()) {
        if (error) *error = tr("Current document has no destination path");
        return false;
    }
    if (!saveFileToPath(tab->getFilePath(), tab, error))
        return false;
    tab->setRecoveryWarning({});
    tab->setRecoveryCheckpointBlocked(false);
    tab->checkpoint();
    checkpointSessionLayout();
    updateStatusBar();
    return true;
}

bool MainWindow::saveCurrentAs(const QString &path, QString *error)
{
    DocumentTab *tab = getCurrentTab();
    if (!tab || path.isEmpty()) {
        if (error) *error = tr("No document or destination path");
        return false;
    }
    if (!saveFileToPath(path, tab, error))
        return false;
    tab->setFilePath(path);
    tab->setRecoveryWarning({});
    tab->setRecoveryCheckpointBlocked(false);
    tab->checkpoint();
    checkpointSessionLayout();
    updateStatusBar();
    return true;
}

void MainWindow::updateWindowTitle() {
    setWindowTitle("Notepad++");
}

// Close actions implementation
void MainWindow::closeTabAction()
{
    QTabWidget *pane = dualViewManager->activePane();
    int currentIndex = pane->currentIndex();
    if (currentIndex >= 0) {
        closeTab(currentIndex, true, pane);
    }
}

void MainWindow::closeAllTabsAction()
{
    if (closeAllTabs()) {
        // Create a new tab after closing all
        createNewTab();
    }
}

void MainWindow::saveAllTabsAction()
{
    bool saveCancelled = false;
    QSet<QString> saved;
    for (DocumentTab *tab : documentViews()) {
        if (!saveCancelled && tab && tab->isDirty() && !saved.contains(tab->documentId()) && !saveTab(tab))
            saveCancelled = true;
        if (tab) saved.insert(tab->documentId());
    }

    // Update status bar after saving all
    updateStatusBar();
}

// Session management functions
CheckpointStatus MainWindow::saveSession() {
    if (!m_sessionManager)
        return CheckpointStatus::NoChanges;
    QSet<QString> checkpointed;
    for (DocumentTab *tab : documentViews()) {
        if (tab && !checkpointed.contains(tab->documentId())) {
            tab->checkpoint();
            checkpointed.insert(tab->documentId());
        }
    }
    checkpointSessionLayout();
    return m_sessionManager->flush();
}

void MainWindow::loadSession() {
    const QString overrideDirectory = QString::fromUtf8(qgetenv("NPP_SESSION_DIR"));
    m_sessionManager = new SessionManager(this, overrideDirectory);
    connect(m_sessionManager, &SessionManager::checkpointFailed, this,
            [this](const QString &message) {
                m_sessionDiagnostics = message;
                statusBar->showMessage(tr("RECOVERY WRITE FAILED: %1").arg(message));
            });
    m_sessionManager->loadSession();
    m_sessionDiagnostics = m_sessionManager->diagnostics().join(QStringLiteral(" | "));

    const QString requestedActiveId = m_sessionManager->activeDocumentId();
    const QVector<RecoveryDocument> recovered = m_sessionManager->documents();
    m_loadingSession = true;
    for (const RecoveryDocument &document : recovered) {
        createNewTab(document.filePath, document.id, document.untitledNumber, false);
        DocumentTab *tab = getCurrentTab();
        if (!tab)
            continue;
        const RecoveryReadResult read = m_sessionManager->readRecoveryContent(document);
        bool payloadUsable = read.success;
        if (read.success) {
            QByteArray restored = read.content;
            DocumentFormat::TextEncoding restoredEncoding = document.encoding;
            DocumentFormat::EolKind restoredEol = document.eol;
            DocumentFormat::EolKind restoredInsertionEol = document.insertionEol;
            if (!document.dirty || document.legacyEncodedSnapshot) {
                const auto decoded = DocumentFormat::decode(read.content);
                if (decoded.success) {
                    restored = decoded.utf8;
                    if (!document.dirty) {
                        restoredEncoding = decoded.encoding;
                        restoredEol = decoded.eol.kind;
                        restoredInsertionEol = decoded.eol.insertion;
                    }
                } else {
                    payloadUsable = false;
                }
            }
            if (!DocumentFormat::isValidUtf8Text(restored))
                payloadUsable = false;
            if (payloadUsable) {
                tab->getEditor()->send(SCI_SETTEXT, 0,
                                       reinterpret_cast<sptr_t>(restored.constData()));
                tab->setDocumentFormat(restoredEncoding, restoredEol, restoredInsertionEol);
                tab->getEditor()->send(SCI_SETSAVEPOINT);
            }
        }
        tab->setRecoveredDirty(document.dirty);
        QString warning;
        switch (document.recoveryState) {
        case RecoveryState::OriginalMissing:
            warning = tr("Recovered copy: original file is missing");
            break;
        case RecoveryState::OriginalChanged:
            warning = tr("Recovered copy: original changed externally; save explicitly to replace it");
            break;
        case RecoveryState::SnapshotMissing:
            warning = tr("Recovery snapshot is missing; available content may be incomplete");
            break;
        case RecoveryState::SnapshotUnreadable:
            warning = tr("Recovery snapshot could not be read; recovery data was preserved");
            break;
        case RecoveryState::Ready:
            break;
        }
        if (!payloadUsable && warning.isEmpty())
            warning = tr("Recovery content is not valid text; recovery data was preserved");
        tab->setRecoveryWarning(warning);
        tab->setRecoveryCheckpointBlocked(
            !payloadUsable || document.recoveryState == RecoveryState::SnapshotMissing ||
            document.recoveryState == RecoveryState::SnapshotUnreadable);
        tab->setSessionManager(m_sessionManager);
        if (payloadUsable && !document.dirty)
            tab->checkpoint();
    }
    m_loadingSession = false;

    const QStringList restoredPrimary = m_sessionManager->primaryDocumentIds();
    const QStringList restoredSecondary = m_sessionManager->secondaryDocumentIds();
    if (!restoredPrimary.isEmpty() || !restoredSecondary.isEmpty()) {
        QHash<QString, DocumentTab *> originals;
        for (auto *view : documentViews()) originals.insert(view->documentId(), view);
        for (const QString &id : restoredSecondary) {
            DocumentTab *original = originals.value(id);
            if (!original) continue;
            if (restoredPrimary.contains(id)) {
                auto *clone = new DocumentTab({}, 0, {}, this, original->sharedState());
                clone->getEditor()->send(SCI_SETDOCPOINTER, 0, original->getEditor()->send(SCI_GETDOCPOINTER));
                clone->setDocumentFormat(original->encoding(), original->eol(), original->insertionEol());
                wireDocumentView(clone);
                secondaryTabWidget->addTab(clone, tabWidget->tabText(tabWidget->indexOf(original)));
            } else {
                const int index = tabWidget->indexOf(original);
                const QString title = tabWidget->tabText(index);
                tabWidget->removeTab(index);
                secondaryTabWidget->addTab(original, title);
            }
        }
        editorViewSplitter->setOrientation(m_sessionManager->splitterOrientation());
        if (m_sessionManager->splitterSizes().size() == 2)
            editorViewSplitter->setSizes(m_sessionManager->splitterSizes());
        dualViewManager->updatePaneVisibility();
        dualViewManager->activate(m_sessionManager->activePane() == QStringLiteral("secondary") && secondaryTabWidget->count()
                                      ? secondaryTabWidget : tabWidget);
    }

    if (tabWidget->count() + secondaryTabWidget->count() == 0)
        createNewTab();
    else {
        QTabWidget *preferred = m_sessionManager->activePane() == QStringLiteral("secondary")
                                   ? secondaryTabWidget : tabWidget;
        QTabWidget *chosenPane = preferred->count() ? preferred : dualViewManager->otherPane(preferred);
        int activeIndex = 0;
        for (QTabWidget *pane : {preferred, dualViewManager->otherPane(preferred)}) {
            for (int i = 0; i < pane->count(); ++i) {
                auto *tab = qobject_cast<DocumentTab *>(pane->widget(i));
                if (tab && tab->documentId() == requestedActiveId) {
                    chosenPane = pane; activeIndex = i; break;
                }
            }
            if (chosenPane->count() > activeIndex) {
                auto *tab = qobject_cast<DocumentTab *>(chosenPane->widget(activeIndex));
                if (tab && tab->documentId() == requestedActiveId) break;
            }
        }
        dualViewManager->activate(chosenPane, activeIndex);
        checkpointSessionLayout();
        updateStatusBar();
    }
}

// Find/Replace functions (restored)
void MainWindow::findNext() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();
    QString findText = findReplaceDialog->findText();

    if (findText.isEmpty()) return;

    // Set up search flags
    int searchFlags = 0;
    if (findReplaceDialog->matchCase()) {
        searchFlags |= SCFIND_MATCHCASE;
    }
    if (findReplaceDialog->wholeWord()) {
        searchFlags |= SCFIND_WHOLEWORD;
    }

    // Get current position
    Scintilla::Position currentPos = editor->send(SCI_GETCURRENTPOS);

    // Set target range to search from current position to end of document
    editor->send(SCI_SETTARGETSTART, currentPos);
    editor->send(SCI_SETTARGETEND, editor->send(SCI_GETTEXTLENGTH));
    editor->send(SCI_SETSEARCHFLAGS, searchFlags);

    // Perform search
    Scintilla::Position foundPos = editor->send(SCI_SEARCHINTARGET,
        static_cast<Scintilla::Position>(findText.length()),
        reinterpret_cast<sptr_t>(findText.toStdString().c_str()));

    if (foundPos != -1) {
        // Select the match
        Scintilla::Position endPos = foundPos + findText.length();
        editor->send(SCI_SETSEL, foundPos, endPos);
        editor->send(SCI_SCROLLCARET);
    } else {
        // Wrap around if enabled
        if (findReplaceDialog->wrapAround()) {
            editor->send(SCI_SETTARGETSTART, 0);
            editor->send(SCI_SETTARGETEND, currentPos);
            editor->send(SCI_SETSEARCHFLAGS, searchFlags);

            foundPos = editor->send(SCI_SEARCHINTARGET,
                static_cast<Scintilla::Position>(findText.length()),
                reinterpret_cast<sptr_t>(findText.toStdString().c_str()));

            if (foundPos != -1) {
                Scintilla::Position endPos = foundPos + findText.length();
                editor->send(SCI_SETSEL, foundPos, endPos);
                editor->send(SCI_SCROLLCARET);
            }
        }
    }
}

void MainWindow::findPrevious() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();
    QString findText = findReplaceDialog->findText();

    if (findText.isEmpty()) return;

    // Set up search flags
    int searchFlags = 0;
    if (findReplaceDialog->matchCase()) {
        searchFlags |= SCFIND_MATCHCASE;
    }
    if (findReplaceDialog->wholeWord()) {
        searchFlags |= SCFIND_WHOLEWORD;
    }

    // Get current position
    Scintilla::Position currentPos = editor->send(SCI_GETCURRENTPOS);

    // Set target range to search from beginning to current position
    editor->send(SCI_SETTARGETSTART, 0);
    editor->send(SCI_SETTARGETEND, currentPos);
    editor->send(SCI_SETSEARCHFLAGS, searchFlags);

    // Perform search backwards
    Scintilla::Position foundPos = editor->send(SCI_SEARCHINTARGET,
        static_cast<Scintilla::Position>(findText.length()),
        reinterpret_cast<sptr_t>(findText.toStdString().c_str()));

    if (foundPos != -1) {
        // Select the match
        Scintilla::Position endPos = foundPos + findText.length();
        editor->send(SCI_SETSEL, foundPos, endPos);
        editor->send(SCI_SCROLLCARET);
    } else {
        // Wrap around if enabled
        if (findReplaceDialog->wrapAround()) {
            editor->send(SCI_SETTARGETSTART, currentPos);
            editor->send(SCI_SETTARGETEND, editor->send(SCI_GETTEXTLENGTH));
            editor->send(SCI_SETSEARCHFLAGS, searchFlags);

            foundPos = editor->send(SCI_SEARCHINTARGET,
                static_cast<Scintilla::Position>(findText.length()),
                reinterpret_cast<sptr_t>(findText.toStdString().c_str()));

            if (foundPos != -1) {
                Scintilla::Position endPos = foundPos + findText.length();
                editor->send(SCI_SETSEL, foundPos, endPos);
                editor->send(SCI_SCROLLCARET);
            }
        }
    }
}

void MainWindow::replace() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();
    QString findText = findReplaceDialog->findText();
    QString replaceText = findReplaceDialog->replaceText();

    if (findText.isEmpty()) return;

    // Set up search flags
    int searchFlags = 0;
    if (findReplaceDialog->matchCase()) {
        searchFlags |= SCFIND_MATCHCASE;
    }
    if (findReplaceDialog->wholeWord()) {
        searchFlags |= SCFIND_WHOLEWORD;
    }

    // Get current position
    Scintilla::Position currentPos = editor->send(SCI_GETCURRENTPOS);

    // Check if we're at a match
    Scintilla::Position anchor = editor->send(SCI_GETANCHOR);
    Scintilla::Position selStart = editor->send(SCI_GETSELECTIONSTART);
    Scintilla::Position selEnd = editor->send(SCI_GETSELECTIONEND);

    bool isSelectionMatch = (selStart != selEnd) &&
        (selStart == anchor) &&
        (selEnd - selStart == static_cast<Scintilla::Position>(findText.length()));

    if (isSelectionMatch) {
        // Get the text at current selection
        char* buffer = new char[findText.length() + 1];
        editor->send(SCI_GETTEXT, findText.length() + 1, reinterpret_cast<sptr_t>(buffer));
        QString selectedText(buffer);
        delete[] buffer;

        if (selectedText == findText) {
            // Replace the selection
            editor->send(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(replaceText.toStdString().c_str()));
            editor->send(SCI_SETSEL, selStart, selStart + replaceText.length());

            // Continue searching from after replacement
            findNext();
        }
    } else {
        // Perform search and replace
        findNext();
    }
}

void MainWindow::replaceAll() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase* editor = currentTab->getEditor();
    const QByteArray findText = findReplaceDialog->findText().toUtf8();
    const QByteArray replaceText = findReplaceDialog->replaceText().toUtf8();

    if (findText.isEmpty()) return;

    int searchFlags = 0;
    if (findReplaceDialog->matchCase())
        searchFlags |= SCFIND_MATCHCASE;
    if (findReplaceDialog->wholeWord())
        searchFlags |= SCFIND_WHOLEWORD;

    EditorUtils::replaceAll(editor, findText, replaceText, searchFlags);
    updateStatusBar();
    refreshPanels();
}

void MainWindow::printFile() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;

    ScintillaEditBase *editor = currentTab->getEditor();
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setOption(QAbstractPrintDialog::PrintSelection,
                     editor->send(SCI_GETSELECTIONSTART) !=
                         editor->send(SCI_GETSELECTIONEND));

    if (dialog.exec() == QDialog::Accepted) {
        if (!PrintHelper::printScintillaDocument(editor, printer))
            QMessageBox::warning(this, tr("Print Error"), tr("Failed to print document."));
    }
}

void MainWindow::functionList() {
    functionListWidget->setVisible(functionListAction->isChecked());
    if (functionListWidget->isVisible())
        refreshPanelContent();
}

void MainWindow::documentMap() {
    documentMapWidget->setVisible(documentMapAction->isChecked());
    if (documentMapWidget->isVisible()) {
        refreshPanelContent();
        refreshPanelViewport();
    }
}

void MainWindow::fileBrowser() {
    fileBrowserWidget->setVisible(fileBrowserAction->isChecked());
}

void MainWindow::documentList() {
    documentListWidget->setVisible(documentListAction->isChecked());
    if (documentListWidget->isVisible())
        refreshDocumentList();
}

void MainWindow::startMacroRecording() {
    DocumentTab *tab = getCurrentTab();
    m_macroManager->startRecording(tab ? tab->getEditor() : nullptr);
}

void MainWindow::stopMacroRecording() {
    m_macroManager->stopRecording();
}

void MainWindow::playMacro() {
    DocumentTab *tab = getCurrentTab();
    m_macroManager->play(tab ? tab->getEditor() : nullptr);
}

void MainWindow::runMacroMultipleTimes() {
    bool accepted = false;
    const int repeats = QInputDialog::getInt(this, tr("Run Macro Multiple Times"),
                                             tr("Number of repetitions:"), 1, 1, 10000, 1,
                                             &accepted);
    if (!accepted)
        return;
    DocumentTab *tab = getCurrentTab();
    m_macroManager->play(tab ? tab->getEditor() : nullptr, repeats);
}

void MainWindow::saveMacro() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save Macro"), tr("Macro name:"),
                                               QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;
    bool overwrite = false;
    if (m_macroManager->savedMacroNames().contains(name)) {
        overwrite = QMessageBox::question(this, tr("Overwrite Macro"),
            tr("A macro named ‘%1’ already exists. Overwrite it?").arg(name),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
        if (!overwrite)
            return;
    }
    if (!m_macroManager->saveCurrent(name, overwrite))
        QMessageBox::warning(this, tr("Save Macro"),
                             tr("The macro could not be saved."));
}

void MainWindow::updateMacroActions()
{
    const bool hasEditor = getCurrentTab() != nullptr;
    const bool recording = m_macroManager->isRecording();
    const bool playing = m_macroManager->isPlaying();
    const bool usable = m_macroManager->hasCurrentMacro() && hasEditor && !recording && !playing;
    startMacroRecordingAction->setEnabled(hasEditor && !recording && !playing);
    stopMacroRecordingAction->setEnabled(recording);
    playMacroAction->setEnabled(usable);
    runMacroMultipleTimesAction->setEnabled(usable);
    saveMacroAction->setEnabled(m_macroManager->hasCurrentMacro() && !recording && !playing);
    savedMacroMenu->setEnabled(!recording && !playing && !m_macroManager->savedMacroNames().isEmpty());
}

void MainWindow::rebuildSavedMacroMenu()
{
    savedMacroMenu->clear();
    for (const QString &name : m_macroManager->savedMacroNames()) {
        QAction *action = savedMacroMenu->addAction(name);
        action->setObjectName(QStringLiteral("savedMacro:") + name);
        connect(action, &QAction::triggered, this, [this, name] {
            if (!m_macroManager->selectSaved(name))
                return;
            DocumentTab *tab = getCurrentTab();
            m_macroManager->play(tab ? tab->getEditor() : nullptr);
        });
    }
    if (savedMacroMenu->isEmpty()) {
        QAction *empty = savedMacroMenu->addAction(tr("(No saved macros)"));
        empty->setEnabled(false);
    }
    updateMacroActions();
}

void MainWindow::updateFormatActions()
{
    DocumentTab *tab = getCurrentTab();
    const bool enabled = tab != nullptr;
    for (QAction *item : encodingActionGroup->actions()) item->setEnabled(enabled);
    for (QAction *item : eolActionGroup->actions()) item->setEnabled(enabled);
    if (!tab) return;
    encodingUtf8Action->setChecked(tab->encoding() == DocumentFormat::TextEncoding::Utf8);
    encodingUtf8BomAction->setChecked(tab->encoding() == DocumentFormat::TextEncoding::Utf8Bom);
    encodingUtf16LeAction->setChecked(tab->encoding() == DocumentFormat::TextEncoding::Utf16Le);
    encodingUtf16BeAction->setChecked(tab->encoding() == DocumentFormat::TextEncoding::Utf16Be);
    encodingWindows1252Action->setChecked(tab->encoding() == DocumentFormat::TextEncoding::Windows1252);
    eolCrLfAction->setChecked(tab->insertionEol() == DocumentFormat::EolKind::CrLf);
    eolLfAction->setChecked(tab->insertionEol() == DocumentFormat::EolKind::Lf);
    eolCrAction->setChecked(tab->insertionEol() == DocumentFormat::EolKind::Cr);
}

void MainWindow::setEncoding(DocumentFormat::TextEncoding encoding)
{
    DocumentTab *tab = getCurrentTab();
    if (!tab || tab->encoding() == encoding) return;
    tab->setDocumentFormat(encoding, tab->eol(), tab->insertionEol());
    tab->markMetadataDirty();
    updateFormatActions();
    updateStatusBar();
}

void MainWindow::convertEols(DocumentFormat::EolKind eol)
{
    DocumentTab *tab = getCurrentTab();
    if (!tab) return;
    const int mode = eol == DocumentFormat::EolKind::CrLf ? SC_EOL_CRLF :
                     eol == DocumentFormat::EolKind::Cr ? SC_EOL_CR : SC_EOL_LF;
    tab->getEditor()->send(SCI_CONVERTEOLS, mode);
    for (DocumentTab *view : documentViews())
        if (view->documentId() == tab->documentId())
            view->getEditor()->send(SCI_SETEOLMODE, mode);
    tab->refreshEolMetadata();
    tab->setDocumentFormat(tab->encoding(), tab->eol(), eol);
    tab->markMetadataDirty();
    updateFormatActions();
    updateStatusBar();
}

void MainWindow::syncVertical() {
    dualViewManager->setVerticalSync(syncVerticalAction->isChecked());
}

void MainWindow::syncHorizontal() {
    dualViewManager->setHorizontalSync(syncHorizontalAction->isChecked());
}

void MainWindow::moveToOtherView()
{
    QTabWidget *source = dualViewManager->activePane();
    auto *tab = qobject_cast<DocumentTab *>(source->currentWidget());
    if (!tab) return;
    QTabWidget *target = dualViewManager->otherPane(source);
    for (int i = 0; i < target->count(); ++i) {
        auto *other = qobject_cast<DocumentTab *>(target->widget(i));
        if (other && other->documentId() == tab->documentId()) {
            const int sourceIndex = source->indexOf(tab);
            source->removeTab(sourceIndex);
            tab->deleteLater();
            dualViewManager->activate(target, i);
            dualViewManager->updatePaneVisibility();
            updateDualViewActions();
            checkpointSessionLayout();
            return;
        }
    }
    const int sourceIndex = source->indexOf(tab);
    const QString title = source->tabText(sourceIndex);
    source->removeTab(sourceIndex);
    const int targetIndex = target->addTab(tab, title);
    dualViewManager->activate(target, targetIndex);
    dualViewManager->updatePaneVisibility();
    updateDualViewActions();
    checkpointSessionLayout();
}

void MainWindow::cloneToOtherView()
{
    QTabWidget *source = dualViewManager->activePane();
    auto *original = qobject_cast<DocumentTab *>(source->currentWidget());
    if (!original) return;
    QTabWidget *target = dualViewManager->otherPane(source);
    for (int i = 0; i < target->count(); ++i) {
        auto *existing = qobject_cast<DocumentTab *>(target->widget(i));
        if (existing && existing->documentId() == original->documentId()) {
            dualViewManager->activate(target, i); return;
        }
    }
    auto *clone = new DocumentTab({}, 0, {}, this, original->sharedState());
    clone->getEditor()->send(SCI_SETDOCPOINTER, 0, original->getEditor()->send(SCI_GETDOCPOINTER));
    clone->setDocumentFormat(original->encoding(), original->eol(), original->insertionEol());
    wireDocumentView(clone);
    const int index = target->addTab(clone, source->tabText(source->currentIndex()));
    dualViewManager->activate(target, index);
    dualViewManager->updatePaneVisibility();
    updateDualViewActions();
    checkpointSessionLayout();
}

void MainWindow::updateDualViewActions()
{
    const bool has = getCurrentTab() != nullptr;
    moveToOtherViewAction->setEnabled(has);
    cloneToOtherViewAction->setEnabled(has && viewCount(getCurrentTab()->documentId()) < 2);
}

void MainWindow::refreshPanels()
{
    refreshDocumentList();
    refreshPanelContent();
    refreshPanelViewport();
}

void MainWindow::refreshDocumentList()
{
    if (!documentListWidget->isVisible())
        return;
    QVector<DocumentListEntry> documents;
    m_documentListIds.clear();
    QSet<QString> seen;
    int activeIndex = -1;
    for (auto *tab : documentViews()) {
        if (!tab)
            continue;
        if (seen.contains(tab->documentId())) continue;
        seen.insert(tab->documentId());
        if (getCurrentTab() && getCurrentTab()->documentId() == tab->documentId()) activeIndex = documents.size();
        const QString name = tab->getFilePath().isEmpty()
            ? QStringLiteral("new %1").arg(tab->getTabNumber())
            : QFileInfo(tab->getFilePath()).fileName();
        documents.push_back({name, tab->getFilePath(), tab->isDirty()});
        m_documentListIds << tab->documentId();
    }
    documentListWidget->setDocuments(documents, activeIndex);
}

void MainWindow::refreshPanelContent()
{
    if (!functionListWidget->isVisible() && !documentMapWidget->isVisible())
        return;
    DocumentTab *tab = getCurrentTab();
    if (!tab) {
        if (functionListWidget->isVisible())
            functionListWidget->setDocument({}, {});
        if (documentMapWidget->isVisible())
            documentMapWidget->setDocument({}, 0, 1);
        return;
    }

    const QString content = QString::fromUtf8(EditorUtils::text(tab->getEditor()));
    if (functionListWidget->isVisible())
        functionListWidget->setDocument(content, tab->getFilePath());
    if (documentMapWidget->isVisible()) {
        const DocumentViewport viewport = EditorUtils::documentViewport(tab->getEditor());
        documentMapWidget->setDocument(content, viewport.firstLine, viewport.lineCount);
    }
}

void MainWindow::refreshPanelViewport()
{
    if (!documentMapWidget->isVisible())
        return;
    DocumentTab *tab = getCurrentTab();
    if (!tab)
        return;
    const DocumentViewport viewport = EditorUtils::documentViewport(tab->getEditor());
    documentMapWidget->setViewport(viewport.firstLine, viewport.lineCount);
}

void MainWindow::schedulePanelContentRefresh()
{
    if (functionListWidget->isVisible() || documentMapWidget->isVisible())
        m_panelContentTimer.start();
}

void MainWindow::navigateToLine(int line)
{
    DocumentTab *tab = getCurrentTab();
    if (!tab)
        return;
    tab->getEditor()->send(SCI_GOTOLINE, qMax(0, line));
    tab->getEditor()->send(SCI_SCROLLCARET);
    refreshPanelViewport();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Application shutdown is a recovery checkpoint, not an implicit Discard.
    // Explicit tab Close / Close All retains the Save/Discard/Cancel workflow.
    CheckpointStatus checkpoint = saveSession();
    while (checkpoint != CheckpointStatus::Durable &&
           checkpoint != CheckpointStatus::NoChanges) {
        QMessageBox prompt(this);
        prompt.setIcon(QMessageBox::Critical);
        prompt.setWindowTitle(tr("Recovery Write Failed"));
        prompt.setText(tr("Unsaved buffers could not be written to recovery storage."));
        prompt.setInformativeText(
            tr("Retry after fixing storage, save files explicitly, or explicitly abandon "
               "the unpersisted data."));
        prompt.setStandardButtons(QMessageBox::Retry | QMessageBox::Save |
                                  QMessageBox::Discard | QMessageBox::Cancel);
        prompt.setDefaultButton(QMessageBox::Retry);
        prompt.button(QMessageBox::Save)->setText(tr("Save All…"));
        prompt.button(QMessageBox::Discard)->setText(tr("Exit and Abandon Recovery"));
        const int decision = prompt.exec();
        if (decision == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (decision == QMessageBox::Discard)
            break;
        if (decision == QMessageBox::Save) {
            bool allSaved = true;
            QSet<QString> saved;
            for (DocumentTab *tab : documentViews()) {
                if (tab && tab->isDirty() && !saved.contains(tab->documentId()) && !saveTab(tab)) {
                    allSaved = false;
                    break;
                }
                if (tab) saved.insert(tab->documentId());
            }
            if (!allSaved) {
                event->ignore();
                return;
            }
        }
        checkpoint = saveSession();
    }
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
    settings.setValue(QStringLiteral("fileBrowser/root"), fileBrowserWidget->rootPath());
    event->accept();
}

void MainWindow::findReplaceClosed() {
    // No implementation needed for this function
}

QMainWindow *createMainWindow(QWidget *parent)
{
    return new MainWindow(parent);
}

bool openFileInMainWindow(QMainWindow *window, const QString &path, QString *error)
{
    auto *mainWindow = qobject_cast<MainWindow *>(window);
    if (!mainWindow) {
        if (error) *error = QStringLiteral("Invalid main window");
        return false;
    }
    return mainWindow->openPath(path, error);
}

bool saveCurrentFileInMainWindow(QMainWindow *window, QString *error)
{
    auto *mainWindow = qobject_cast<MainWindow *>(window);
    if (!mainWindow) {
        if (error) *error = QStringLiteral("Invalid main window");
        return false;
    }
    return mainWindow->saveCurrent(error);
}

bool saveCurrentFileAsInMainWindow(QMainWindow *window, const QString &path, QString *error)
{
    auto *mainWindow = qobject_cast<MainWindow *>(window);
    if (!mainWindow) {
        if (error) *error = QStringLiteral("Invalid main window");
        return false;
    }
    return mainWindow->saveCurrentAs(path, error);
}

#include "mainwindow.moc"
