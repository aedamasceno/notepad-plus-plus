#include <QApplication>
#include <QMainWindow>
#include "ScintillaEdit.h"   // Scintilla Qt widget

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    ScintillaEdit* editor = new ScintillaEdit(&window);
    window.setCentralWidget(editor);
    window.resize(800, 600);
    window.show();

    return app.exec();
}
