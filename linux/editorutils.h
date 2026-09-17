#pragma once

#include <QByteArray>
#include <QString>

class ScintillaEditBase;

namespace EditorUtils {
QByteArray text(ScintillaEditBase *editor);
int replaceAll(ScintillaEditBase *editor, const QByteArray &findText,
               const QByteArray &replaceText, int searchFlags);
bool writeToFile(ScintillaEditBase *editor, const QString &filePath,
                 QString *errorMessage = nullptr);
}
