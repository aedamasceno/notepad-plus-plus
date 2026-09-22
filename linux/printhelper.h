#ifndef PRINTHELPER_H
#define PRINTHELPER_H

class QPrinter;
class ScintillaEditBase;

class PrintHelper {
public:
    static bool printScintillaDocument(ScintillaEditBase *editor, QPrinter &printer);
};

#endif // PRINTHELPER_H
