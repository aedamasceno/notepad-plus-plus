#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include "ScintillaEditBase.h"   // Scintilla Qt widget base class

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    ScintillaEditBase* editor = new ScintillaEditBase(&window);
    window.setCentralWidget(editor);
    
    // Set window title
    window.setWindowTitle("Notepad++");
    
    // Create menu bar
    QMenuBar* menuBar = window.menuBar();
    
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
    QToolBar* toolBar = window.addToolBar("Main Toolbar");
    
    window.resize(800, 600);
    window.show();

    return app.exec();
}
