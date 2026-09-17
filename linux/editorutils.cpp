#include "editorutils.h"

#include "ScintillaEditBase.h"
#include <QSaveFile>

namespace EditorUtils {

QByteArray text(ScintillaEditBase *editor)
{
    const auto length = editor->send(SCI_GETTEXTLENGTH);
    QByteArray result(static_cast<qsizetype>(length) + 1, '\0');
    editor->send(SCI_GETTEXT, length + 1, reinterpret_cast<sptr_t>(result.data()));
    result.truncate(static_cast<qsizetype>(length));
    return result;
}

DocumentViewport documentViewport(ScintillaEditBase *editor)
{
    if (!editor)
        return {};
    const int documentLines = qMax(1, int(editor->send(SCI_GETLINECOUNT)));
    const int firstDisplayLine = qMax(0, int(editor->send(SCI_GETFIRSTVISIBLELINE)));
    const int displayLines = qMax(1, int(editor->send(SCI_LINESONSCREEN)));
    const int firstDocumentLine = qBound(
        0, int(editor->send(SCI_DOCLINEFROMVISIBLE, firstDisplayLine)), documentLines - 1);
    const int lastDocumentLine = qBound(
        firstDocumentLine,
        int(editor->send(SCI_DOCLINEFROMVISIBLE, firstDisplayLine + displayLines - 1)),
        documentLines - 1);
    return {firstDocumentLine, lastDocumentLine - firstDocumentLine + 1};
}

int replaceAll(ScintillaEditBase *editor, const QByteArray &findText,
               const QByteArray &replaceText, int searchFlags)
{
    if (!editor || findText.isEmpty())
        return 0;

    editor->send(SCI_SETSEARCHFLAGS, searchFlags);
    Scintilla::Position searchStart = 0;
    int replacementCount = 0;

    while (searchStart <= editor->send(SCI_GETTEXTLENGTH)) {
        const Scintilla::Position documentEnd = editor->send(SCI_GETTEXTLENGTH);
        editor->send(SCI_SETTARGETSTART, searchStart);
        editor->send(SCI_SETTARGETEND, documentEnd);
        const Scintilla::Position matchStart = editor->send(
            SCI_SEARCHINTARGET, static_cast<uptr_t>(findText.size()),
            reinterpret_cast<sptr_t>(findText.constData()));
        if (matchStart < 0)
            break;

        const Scintilla::Position replacedLength = editor->send(
            SCI_REPLACETARGET, static_cast<uptr_t>(replaceText.size()),
            reinterpret_cast<sptr_t>(replaceText.constData()));
        ++replacementCount;

        // Continue after the inserted bytes. This both handles shrinking replacements
        // and prevents matching text introduced by the replacement indefinitely.
        searchStart = matchStart + replacedLength;
    }

    return replacementCount;
}

bool writeToFile(ScintillaEditBase *editor, const QString &filePath,
                 QString *errorMessage)
{
    if (!editor || filePath.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No editor or destination path");
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    const QByteArray bytes = text(editor);
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return true;
}

} // namespace EditorUtils
