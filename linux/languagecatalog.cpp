#include "languagecatalog.h"
#include "ScintillaEditBase.h"
#include "ILexer.h"
#include "Lexilla.h"

#include <QFileInfo>

const QVector<LanguageDefinition> &LanguageCatalog::definitions()
{
    static const QVector<LanguageDefinition> value = {
        {"plain", QObject::tr("Normal Text"), "null", {}},
        {"c", QObject::tr("C"), "cpp", {"c", "h"}},
        {"cpp", QObject::tr("C++"), "cpp", {"cc", "cpp", "cxx", "hh", "hpp", "hxx"}},
        {"csharp", QObject::tr("C#"), "cpp", {"cs"}},
        {"java", QObject::tr("Java"), "cpp", {"java"}},
        {"javascript", QObject::tr("JavaScript"), "cpp", {"js", "mjs", "cjs", "jsx"}},
        {"json", QObject::tr("JSON"), "json", {"json", "json5"}},
        {"python", QObject::tr("Python"), "python", {"py", "pyw"}},
        {"html", QObject::tr("HTML"), "hypertext", {"html", "htm"}},
        {"xml", QObject::tr("XML"), "xml", {"xml", "xhtml", "svg"}},
        {"css", QObject::tr("CSS"), "css", {"css"}},
        {"bash", QObject::tr("Bash / Shell"), "bash", {"sh", "bash", "zsh"}},
        {"sql", QObject::tr("SQL"), "sql", {"sql"}},
        {"properties", QObject::tr("INI / Properties"), "props", {"ini", "cfg", "conf", "properties"}},
        {"yaml", QObject::tr("YAML"), "yaml", {"yaml", "yml"}},
        {"markdown", QObject::tr("Markdown"), "markdown", {"md", "markdown"}},
    };
    return value;
}

QStringList LanguageCatalog::ids()
{
    QStringList result;
    for (const auto &language : definitions()) result << language.id;
    return result;
}

const LanguageDefinition *LanguageCatalog::find(const QString &id)
{
    for (const auto &language : definitions()) if (language.id == id) return &language;
    return nullptr;
}

QString LanguageCatalog::detectForPath(const QString &path)
{
    const QString extension = QFileInfo(path).suffix().toLower();
    for (const auto &language : definitions())
        if (!extension.isEmpty() && language.extensions.contains(extension)) return language.id;
    return QStringLiteral("plain");
}

Scintilla::ILexer5 *LanguageCatalog::createLexer(const QString &id)
{
    const auto *language = find(id);
    if (!language || language->lexerName.isEmpty()) return nullptr;
    const QByteArray name = language->lexerName.toLatin1();
    return CreateLexer(name.constData());
}

bool LanguageCatalog::apply(ScintillaEditBase *editor, const QString &id)
{
    if (!editor || !find(id)) return false;
    auto *lexer = createLexer(id);
    if (!lexer) return false;
    editor->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
    editor->send(SCI_COLOURISE, 0, -1);
    return true;
}
