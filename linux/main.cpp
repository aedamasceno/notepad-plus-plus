#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QTabWidget>
#include <QCloseEvent>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QLayout>
#include <QKeySequence>
#include "ScintillaEditBase.h"
#include "findreplace.h"

class DocumentTab : public QWidget {
    Q_OBJECT

public:
    DocumentTab(const QString& filePath = "", int tabNumber = 0, QWidget* parent = nullptr) 
        : QWidget(parent), currentFilePath(filePath), isModified(false), tabNumber(tabNumber) {
        setupUI();
        setupActions();
        updateTitle();
    }

    ScintillaEditBase* getEditor() { return editor; }
    QString getFilePath() const { return currentFilePath; }
    bool isDirty() const { return isModified; }
    void setDirty(bool dirty) { isModified = dirty; updateTitle(); }
    void setFilePath(const QString& path) { currentFilePath = path; updateTitle(); }
    int getTabNumber() const { return tabNumber; }

private:
    void setupUI() {
        editor = new ScintillaEditBase(this);
        
        // Set up layout
        QVBoxLayout* layout = new QVBoxLayout();
        layout->addWidget(editor);
        layout->setContentsMargins(0, 0, 0, 0);
        setLayout(layout);
    }

    void setupActions() {
        // Connect editor signals to track modifications
        connect(editor, &ScintillaEditBase::savePointChanged, this, [this](bool dirty) {
            isModified = dirty;
            updateTitle();
        });
    }

    void updateTitle() {
        emit titleChanged();
    }

signals:
    void titleChanged();

private:
    ScintillaEditBase* editor;
    QString currentFilePath;
    bool isModified;
    int tabNumber;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent), nextUntitledNumber(1) {
        setupUI();
        setupActions();
        createNewTab(); // Ensure at least one tab exists
        findReplaceDialog = new FindReplaceDialog(this);
        connect(findReplaceDialog, &FindReplaceDialog::findNext, this, &MainWindow::findNext);
        connect(findReplaceDialog, &FindReplaceDialog::findPrevious, this, &MainWindow::findPrevious);
        connect(findReplaceDialog, &FindReplaceDialog::replace, this, &MainWindow::replace);
        connect(findReplaceDialog, &FindReplaceDialog::replaceAll, this, &MainWindow::replaceAll);
        connect(findReplaceDialog, &FindReplaceDialog::closed, this, &MainWindow::findReplaceClosed);
    }

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
    void replace();

    // Tab management
    void tabChanged(int index);
    void tabCloseRequested(int index);
    void documentTitleChanged();
    void updateStatusBar();

private:
    void setupUI();
    void setupActions();
    void updateWindowTitle();
    bool loadFile(const QString &filePath);
    bool saveFileToPath(const QString &filePath);
    void createNewTab(const QString& filePath = "");
    int findTabIndexForFilePath(const QString& filePath);
    bool closeTab(int index);
    bool closeAllTabs();
    DocumentTab* getCurrentTab() const;
    void updateEditActionsEnabled();
    
    // Find/Replace functions
    void findNext();
    void findPrevious();
    void replace();
    void replaceAll();
    void findReplaceClosed();

    QTabWidget* tabWidget;
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
    
    // Counter for untitled documents
    int nextUntitledNumber;
    
    // Find/Replace dialog
    FindReplaceDialog *findReplaceDialog;
};

void MainWindow::setupUI() {
    tabWidget = new QTabWidget(this);
    // Enable close buttons on tabs
    tabWidget->setTabsClosable(true);
    setCentralWidget(tabWidget);
    
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
}

void MainWindow::setupActions() {
    // File actions
    newAction = new QAction("&New", this);
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
    connect(replaceAction, &QAction::triggered, this, &MainWindow::replace);

    // Connect tab signals
    connect(tabWidget, &QTabWidget::currentChanged, this, &MainWindow::tabChanged);
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::tabCloseRequested);

    // Add actions to menus
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
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
    replaceAction->setShortcut(QKeySequence::Replace);
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
            tabWidget->setCurrentIndex(existingIndex);
            return;
        }
        
        createNewTab(fileName);
    }
}

void MainWindow::saveFile() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;
    
    if (currentTab->getFilePath().isEmpty()) {
        saveAsFile();
    } else {
        saveFileToPath(currentTab->getFilePath());
    }
}

void MainWindow::saveAsFile() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;
    
    QString fileName = QFileDialog::getSaveFileName(this, "Save File", "", "All Files (*)");
    if (!fileName.isEmpty()) {
        if (saveFileToPath(fileName)) {
            currentTab->setFilePath(fileName);
            currentTab->setDirty(false);
        }
    }
}

void MainWindow::exitApp() {
    if (closeAllTabs()) {
        close();
    }
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

void MainWindow::replace() {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return;
    
    // Show replace dialog
    findReplaceDialog->showReplace();
    findReplaceDialog->setFindText("");
    findReplaceDialog->show();
    findReplaceDialog->raise();
    findReplaceDialog->activateWindow();
}

void MainWindow::tabChanged(int index) {
    updateEditActionsEnabled();
    updateStatusBar();
}

void MainWindow::tabCloseRequested(int index) {
    closeTab(index);
}

void MainWindow::documentTitleChanged() {
    DocumentTab* tab = qobject_cast<DocumentTab*>(sender());
    if (tab) {
        int index = tabWidget->indexOf(tab);
        if (index != -1) {
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
            tabWidget->setTabText(index, title);
        }
    }
    updateStatusBar();
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
    
    // Get EOL mode
    int eolMode = editor->send(SCI_GETEOLMODE);
    QString eolStr;
    switch (eolMode) {
        case 0: // SC_EOL_CRLF
            eolStr = "Windows (CR LF)";
            break;
        case 1: // SC_EOL_LF
            eolStr = "Unix (LF)";
            break;
        case 2: // SC_EOL_CR
            eolStr = "Macintosh (CR)";
            break;
        default:
            eolStr = "Unknown";
    }
    
    // Get insert/overwrite mode
    bool overwrite = editor->send(SCI_GETOVERTYPE);
    QString modeStr = overwrite ? "OVR" : "INS";
    
    // Format status bar text
    QString statusText = QString("Ln %1, Col %2    Sel %3    Lines %4    %5    UTF-8    %6")
                         .arg(line + 1)
                         .arg(col + 1)
                         .arg(selLength)
                         .arg(lineCount)
                         .arg(eolStr)
                         .arg(modeStr);
    
    statusBar->showMessage(statusText);
}

void MainWindow::createNewTab(const QString& filePath) {
    DocumentTab* newTab = new DocumentTab(filePath, nextUntitledNumber, this);
    
    // Connect the tab's titleChanged signal to update the tab text
    connect(newTab, &DocumentTab::titleChanged, this, &MainWindow::documentTitleChanged);
    
    // Connect editor signals for status bar updates
    connect(newTab->getEditor(), &ScintillaEditBase::notify, this, &MainWindow::updateStatusBar);
    
    QString title;
    if (filePath.isEmpty()) {
        // For new untitled documents, use sequential naming
        title = QString("new %1").arg(nextUntitledNumber);
        nextUntitledNumber++;
    } else {
        // Check if file is already open
        int existingIndex = findTabIndexForFilePath(filePath);
        if (existingIndex != -1) {
            tabWidget->setCurrentIndex(existingIndex);
            return;
        }
        title = QFileInfo(filePath).fileName();
    }
    
    int index = tabWidget->addTab(newTab, title);
    tabWidget->setCurrentIndex(index);
    
    // Update tab text to include asterisk if needed
    documentTitleChanged();
    
    // Update status bar for new tab
    updateStatusBar();
}

int MainWindow::findTabIndexForFilePath(const QString& filePath) {
    for (int i = 0; i < tabWidget->count(); ++i) {
        DocumentTab* tab = qobject_cast<DocumentTab*>(tabWidget->widget(i));
        if (tab && tab->getFilePath() == filePath) {
            return i;
        }
    }
    return -1;
}

bool MainWindow::closeTab(int index) {
    DocumentTab* tab = qobject_cast<DocumentTab*>(tabWidget->widget(index));
    if (!tab) return false;
    
    // If the tab is modified, ask user
    if (tab->isDirty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Save Changes");
        msgBox.setText("The document has been modified.");
        msgBox.setInformativeText("Do you want to save your changes?");
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);
        
        int ret = msgBox.exec();
        switch (ret) {
            case QMessageBox::Save:
                if (!saveFileToPath(tab->getFilePath())) {
                    return false; // Save failed, cancel closing
                }
                break;
            case QMessageBox::Discard:
                break; // Just close
            case QMessageBox::Cancel:
                return false; // Cancel closing
        }
    }
    
    tabWidget->removeTab(index);
    
    // Ensure at least one tab remains
    if (tabWidget->count() == 0) {
        createNewTab();
    }
    
    return true;
}

bool MainWindow::closeAllTabs() {
    // Check all tabs for unsaved changes
    for (int i = tabWidget->count() - 1; i >= 0; --i) {
        if (!closeTab(i)) {
            return false; // Cancelled by user
        }
    }
    return true;
}

DocumentTab* MainWindow::getCurrentTab() const {
    QWidget* currentWidget = tabWidget->currentWidget();
    return qobject_cast<DocumentTab*>(currentWidget);
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

bool MainWindow::loadFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error", "Could not open file for reading.");
        return false;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    DocumentTab* currentTab = getCurrentTab();
    if (currentTab) {
        currentTab->getEditor()->send(SCI_CLEARALL);
        currentTab->getEditor()->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(content.toStdString().c_str()));
        currentTab->setFilePath(filePath);
        currentTab->setDirty(false);
    }
    
    // Update status bar after loading file
    updateStatusBar();
    
    return true;
}

bool MainWindow::saveFileToPath(const QString &filePath) {
    DocumentTab* currentTab = getCurrentTab();
    if (!currentTab) return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Error", "Could not open file for writing.");
        return false;
    }

    // Get text from Scintilla editor
    Scintilla::Position length = currentTab->getEditor()->send(SCI_GETTEXTLENGTH);
    if (length > 0) {
        char* buffer = new char[length + 1];
        currentTab->getEditor()->send(SCI_GETTEXT, length + 1, reinterpret_cast<sptr_t>(buffer));
        QString content(buffer);
        delete[] buffer;
        
        QTextStream out(&file);
        out << content;
    } else {
        // File is empty
        QTextStream out(&file);
        out << "";
    }
    file.close();
    
    return true;
}

void MainWindow::updateWindowTitle() {
    setWindowTitle("Notepad++");
}

// Find/Replace functions
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
    
    // Perform search
    Scintilla::Position foundPos = editor->send(SCI_FINDTEXT, searchFlags, 
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
            
            foundPos = editor->send(SCI_FINDTEXT, searchFlags, 
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
    
    // Perform search backwards
    Scintilla::Position foundPos = editor->send(SCI_FINDTEXT, searchFlags, 
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
            
            foundPos = editor->send(SCI_FINDTEXT, searchFlags, 
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
    
    // Begin undo action
    editor->send(SCI_BEGINUNDOACTION);
    
    // Set target to entire document
    editor->send(SCI_SETTARGETSTART, 0);
    editor->send(SCI_SETTARGETEND, editor->send(SCI_GETTEXTLENGTH));
    
    // Replace all occurrences
    Scintilla::Position replaceCount = editor->send(SCI_REPLACETARGET, searchFlags, 
        reinterpret_cast<sptr_t>(replaceText.toStdString().c_str()));
    
    // End undo action
    editor->send(SCI_ENDUNDOACTION);
    
    if (replaceCount > 0) {
        QMessageBox::information(this, "Replace All", QString("%1 occurrences replaced").arg(replaceCount));
    }
}

void MainWindow::findReplaceClosed() {
    // Nothing to do here for now
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.resize(800, 600);
    window.show();

    return app.exec();
}

#include "main.moc"
