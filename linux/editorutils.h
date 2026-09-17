#pragma once

#include <QByteArray>
#include <QString>

class ScintillaEditBase;

struct DocumentViewport {
    int firstLine = 0;
    int lineCount = 1;
};

namespace EditorUtils {
QByteArray text(ScintillaEditBase *editor);
DocumentViewport documentViewport(ScintillaEditBase *editor);
int replaceAll(ScintillaEditBase *editor, const QByteArray &findText,
               const QByteArray &replaceText, int searchFlags);
bool writeToFile(ScintillaEditBase *editor, const QString &filePath,
                 QString *errorMessage = nullptr);
}
