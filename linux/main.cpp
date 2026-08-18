#include <QApplication>
#include <QMainWindow>
#include "ScintillaEditBase.h"   // Scintilla Qt widget base class

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    ScintillaEditBase* editor = new ScintillaEditBase(&window);
    window.setCentralWidget(editor);
    window.resize(800, 600);
    window.show();

    return app.exec();
}
