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
#include "ScintillaEditBase.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent), currentFilePath("") {
        setupUI();
        setupActions();
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

private:
    void setupUI();
    void setupActions();
    void updateWindowTitle();
    bool loadFile(const QString &filePath);
    bool saveFileToPath(const QString &filePath);

    ScintillaEditBase* editor;
    QString currentFilePath;

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
};

void MainWindow::setupUI() {
    editor = new ScintillaEditBase(this);
    setCentralWidget(editor);
    
    // Set window title
    setWindowTitle("Notepad++");
    
    // Create menu bar
    QMenuBar* menuBar = this->menuBar();
    
    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");
    
    // Edit menu
    QMenu* editMenu = menuBar->addMenu("&Edit");
    
    // Search menu
    QMenu* searchMenu = menuBar->addMenu("&Search");
    
    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");
    
    // Encoding menu
    QMenu* encodingMenu = menuBar->addMenu("&Encoding");
    
    // Language menu
    QMenu* languageMenu = menuBar->addMenu("&Language");
    
    // Settings menu
    QMenu* settingsMenu = menuBar->addMenu("&Settings");
    
    // Tools menu
    QMenu* toolsMenu = menuBar->addMenu("&Tools");
    
    // Macro menu
    QMenu* macroMenu = menuBar->addMenu("&Macro");
    
    // Run menu
    QMenu* runMenu = menuBar->addMenu("&Run");
    
    // Plugins menu
    QMenu* pluginsMenu = menuBar->addMenu("&Plugins");
    
    // Window menu
    QMenu* windowMenu = menuBar->addMenu("&Window");
    
    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&?");
    
    // Create toolbar container
    QToolBar* toolBar = addToolBar("Main Toolbar");
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

    // Add actions to menus
    menuBar()->findChild<QMenu*>("&File")->addAction(newAction);
    menuBar()->findChild<QMenu*>("&File")->addAction(openAction);
    menuBar()->findChild<QMenu*>("&File")->addAction(saveAction);
    menuBar()->findChild<QMenu*>("&File")->addAction(saveAsAction);
    menuBar()->findChild<QMenu*>("&File")->addSeparator();
    menuBar()->findChild<QMenu*>("&File")->addAction(exitAction);

    menuBar()->findChild<QMenu*>("&Edit")->addAction(undoAction);
    menuBar()->findChild<QMenu*>("&Edit")->addAction(redoAction);
    menuBar()->findChild<QMenu*>("&Edit")->addSeparator();
    menuBar()->findChild<QMenu*>("&Edit")->addAction(cutAction);
    menuBar()->findChild<QMenu*>("&Edit")->addAction(copyAction);
    menuBar()->findChild<QMenu*>("&Edit")->addAction(pasteAction);
    menuBar()->findChild<QMenu*>("&Edit")->addSeparator();
    menuBar()->findChild<QMenu*>("&Edit")->addAction(selectAllAction);

    // Add actions to toolbar
    addToolBarBreak();
    QToolBar* toolBar = findChild<QToolBar*>("Main Toolbar");
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
}

void MainWindow::newFile() {
    editor->send(SCI_CLEARALL);
    currentFilePath = "";
    updateWindowTitle();
}

void MainWindow::openFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open File", "", "All Files (*)");
    if (!fileName.isEmpty()) {
        loadFile(fileName);
    }
}

void MainWindow::saveFile() {
    if (currentFilePath.isEmpty()) {
        saveAsFile();
    } else {
        saveFileToPath(currentFilePath);
    }
}

void MainWindow::saveAsFile() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save File", "", "All Files (*)");
    if (!fileName.isEmpty()) {
        if (saveFileToPath(fileName)) {
            currentFilePath = fileName;
            updateWindowTitle();
        }
    }
}

void MainWindow::exitApp() {
    close();
}

void MainWindow::undo() {
    editor->send(SCI_UNDO);
}

void MainWindow::redo() {
    editor->send(SCI_REDO);
}

void MainWindow::cut() {
    editor->send(SCI_CUT);
}

void MainWindow::copy() {
    editor->send(SCI_COPY);
}

void MainWindow::paste() {
    editor->send(SCI_PASTE);
}

void MainWindow::selectAll() {
    editor->send(SCI_SELECTALL);
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

    editor->send(SCI_CLEARALL);
    editor->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(content.toStdString().c_str()));
    
    currentFilePath = filePath;
    updateWindowTitle();
    return true;
}

bool MainWindow::saveFileToPath(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Error", "Could not open file for writing.");
        return false;
    }

    QString content = editor->send(SCI_GETTEXT, 0, 0).toString();
    QTextStream out(&file);
    out << content;
    file.close();
    
    return true;
}

void MainWindow::updateWindowTitle() {
    if (currentFilePath.isEmpty()) {
        setWindowTitle("Notepad++");
    } else {
        QFileInfo fileInfo(currentFilePath);
        setWindowTitle(fileInfo.fileName() + " - Notepad++");
    }
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
