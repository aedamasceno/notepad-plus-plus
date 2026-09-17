#include "editorutils.h"
#include "functionlist.h"
#include "ScintillaEditBase.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
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

void setText(ScintillaEditBase &editor, const QByteArray &text)
{
    editor.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
}

void testReplaceAll()
{
    ScintillaEditBase editor;
    setText(editor, "one one one");
    expect(EditorUtils::replaceAll(&editor, "one", "two", 0) == 3,
           "replaceAll replaces multiple matches");
    expect(EditorUtils::text(&editor) == "two two two", "multiple replacement content");

    setText(editor, "nothing here");
    expect(EditorUtils::replaceAll(&editor, "absent", "x", 0) == 0,
           "replaceAll reports no match");
    expect(EditorUtils::text(&editor) == "nothing here", "no-match leaves content unchanged");

    setText(editor, QStringLiteral("café café").toUtf8());
    expect(EditorUtils::replaceAll(&editor, QStringLiteral("café").toUtf8(),
                                   QStringLiteral("茶").toUtf8(), 0) == 2,
           "replaceAll uses UTF-8 byte lengths");
    expect(EditorUtils::text(&editor) == QStringLiteral("茶 茶").toUtf8(),
           "Unicode replacement content");

    setText(editor, "x x");
    expect(EditorUtils::replaceAll(&editor, "x", "xx", 0) == 2,
           "replacement containing search text terminates");
    expect(EditorUtils::text(&editor) == "xx xx", "self-containing replacement content");

    setText(editor, "cat scatter cat");
    expect(EditorUtils::replaceAll(&editor, "cat", "dog", SCFIND_WHOLEWORD) == 2,
           "replaceAll honors search options");
    expect(EditorUtils::text(&editor) == "dog scatter dog", "whole-word replacement content");
}

void testExactBackgroundDocumentWrites()
{
    ScintillaEditBase first;
    ScintillaEditBase second;
    setText(first, "first document\n");
    setText(second, QStringLiteral("second λ document\n").toUtf8());
    QTemporaryDir directory;
    const QString firstPath = directory.filePath(QStringLiteral("first.txt"));
    const QString secondPath = directory.filePath(QStringLiteral("second.txt"));
    expect(EditorUtils::writeToFile(&first, firstPath), "save first background editor");
    expect(EditorUtils::writeToFile(&second, secondPath), "save second background editor");
    QFile firstFile(firstPath);
    QFile secondFile(secondPath);
    expect(firstFile.open(QIODevice::ReadOnly), "open first saved file");
    expect(secondFile.open(QIODevice::ReadOnly), "open second saved file");
    expect(firstFile.readAll() == "first document\n", "first file has first editor bytes");
    expect(secondFile.readAll() == QStringLiteral("second λ document\n").toUtf8(),
           "second file has second editor bytes");
}

void testFunctionParsing()
{
    expect(parseFunctions("int alpha() {\n}\n", "a.cpp").size() == 1,
           "parse C++ live buffer");
    expect(parseFunctions("public void beta() {\n}\n", "A.java").size() == 1,
           "parse Java live buffer");
    expect(parseFunctions("function gamma() {\n}\nconst delta = () => 1;\n", "a.js").size() == 2,
           "parse JavaScript live buffer");
    expect(parseFunctions("def epsilon():\n    pass\n", "a.py").size() == 1,
           "parse Python live buffer");
    expect(parseFunctions("def unsaved():\n    pass\n", "").size() == 1,
           "parse unsaved Python buffer");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testReplaceAll();
    testExactBackgroundDocumentWrites();
    testFunctionParsing();
    if (failures == 0)
        std::puts("All Linux panel tests passed");
    return failures == 0 ? 0 : 1;
}
