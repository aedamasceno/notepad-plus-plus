#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace Scintilla { class ILexer5; }
class ScintillaEditBase;

struct LanguageDefinition {
    QString id;
    QString displayName;
    QString lexerName;
    QStringList extensions;
};

namespace LanguageCatalog {
const QVector<LanguageDefinition> &definitions();
QStringList ids();
const LanguageDefinition *find(const QString &id);
QString detectForPath(const QString &path);
Scintilla::ILexer5 *createLexer(const QString &id);
bool apply(ScintillaEditBase *editor, const QString &id);
}
