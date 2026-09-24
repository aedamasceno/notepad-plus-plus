#include "editorpreferences.h"
#include "ScintillaEditBase.h"

#include <QSettings>

bool EditorPreferences::operator==(const EditorPreferences &o) const
{
    return fontFamily == o.fontFamily && fontSize == o.fontSize && tabWidth == o.tabWidth &&
           useTabs == o.useTabs && wordWrap == o.wordWrap && lineNumbers == o.lineNumbers &&
           showWhitespace == o.showWhitespace && indentGuides == o.indentGuides &&
           defaultEol == o.defaultEol && defaultEncoding == o.defaultEncoding &&
           toolbarVisible == o.toolbarVisible &&
           statusBarVisible == o.statusBarVisible && rememberWindowState == o.rememberWindowState;
}

namespace {
template <typename T>
T value(const QSettings &settings, const char *key, const T &fallback)
{
    return settings.value(QString::fromLatin1(key), QVariant::fromValue(fallback)).template value<T>();
}

int boundedInteger(const QSettings &settings, const char *key, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int candidate = settings.value(QString::fromLatin1(key), fallback).toInt(&ok);
    return ok ? qBound(minimum, candidate, maximum) : fallback;
}

bool booleanValue(const QSettings &settings, const char *key, bool fallback)
{
    const QVariant raw = settings.value(QString::fromLatin1(key));
    if (!raw.isValid()) return fallback;
    if (raw.metaType().id() == QMetaType::Bool) return raw.toBool();
    if (raw.metaType().id() == QMetaType::Int || raw.metaType().id() == QMetaType::UInt ||
        raw.metaType().id() == QMetaType::LongLong || raw.metaType().id() == QMetaType::ULongLong) {
        bool ok = false;
        const qlonglong number = raw.toLongLong(&ok);
        return ok && (number == 0 || number == 1) ? number == 1 : fallback;
    }
    const QString text = raw.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) return false;
    return fallback;
}
}

EditorPreferences EditorPreferencesStore::load(const QSettings &settings)
{
    EditorPreferences p;
    const QString fontFamily = value(settings, "preferences/fontFamily", p.fontFamily).trimmed();
    if (!fontFamily.isEmpty()) p.fontFamily = fontFamily;
    p.fontSize = boundedInteger(settings, "preferences/fontSize", p.fontSize, 6, 72);
    p.tabWidth = boundedInteger(settings, "preferences/tabWidth", p.tabWidth, 1, 16);
    p.useTabs = booleanValue(settings, "preferences/useTabs", p.useTabs);
    p.wordWrap = booleanValue(settings, "preferences/wordWrap", p.wordWrap);
    p.lineNumbers = booleanValue(settings, "preferences/lineNumbers", p.lineNumbers);
    p.showWhitespace = booleanValue(settings, "preferences/showWhitespace", p.showWhitespace);
    p.indentGuides = booleanValue(settings, "preferences/indentGuides", p.indentGuides);
    const int eol = value(settings, "preferences/defaultEol", static_cast<int>(p.defaultEol));
    if (eol == static_cast<int>(DocumentFormat::EolKind::Lf) ||
        eol == static_cast<int>(DocumentFormat::EolKind::CrLf) ||
        eol == static_cast<int>(DocumentFormat::EolKind::Cr))
        p.defaultEol = static_cast<DocumentFormat::EolKind>(eol);
    const int encoding = value(settings, "preferences/defaultEncoding",
                               static_cast<int>(p.defaultEncoding));
    if (encoding >= static_cast<int>(DocumentFormat::TextEncoding::Utf8) &&
        encoding <= static_cast<int>(DocumentFormat::TextEncoding::Windows1252))
        p.defaultEncoding = static_cast<DocumentFormat::TextEncoding>(encoding);
    p.toolbarVisible = booleanValue(settings, "preferences/toolbarVisible", p.toolbarVisible);
    p.statusBarVisible = booleanValue(settings, "preferences/statusBarVisible", p.statusBarVisible);
    p.rememberWindowState = booleanValue(settings, "preferences/rememberWindowState", p.rememberWindowState);
    return p;
}

void EditorPreferencesStore::save(QSettings &settings, const EditorPreferences &p)
{
    settings.setValue(QStringLiteral("preferences/fontFamily"), p.fontFamily);
    settings.setValue(QStringLiteral("preferences/fontSize"), p.fontSize);
    settings.setValue(QStringLiteral("preferences/tabWidth"), p.tabWidth);
    settings.setValue(QStringLiteral("preferences/useTabs"), p.useTabs);
    settings.setValue(QStringLiteral("preferences/wordWrap"), p.wordWrap);
    settings.setValue(QStringLiteral("preferences/lineNumbers"), p.lineNumbers);
    settings.setValue(QStringLiteral("preferences/showWhitespace"), p.showWhitespace);
    settings.setValue(QStringLiteral("preferences/indentGuides"), p.indentGuides);
    settings.setValue(QStringLiteral("preferences/defaultEol"), static_cast<int>(p.defaultEol));
    settings.setValue(QStringLiteral("preferences/defaultEncoding"), static_cast<int>(p.defaultEncoding));
    settings.setValue(QStringLiteral("preferences/toolbarVisible"), p.toolbarVisible);
    settings.setValue(QStringLiteral("preferences/statusBarVisible"), p.statusBarVisible);
    settings.setValue(QStringLiteral("preferences/rememberWindowState"), p.rememberWindowState);
}

void EditorPreferencesStore::apply(ScintillaEditBase *editor, const EditorPreferences &p)
{
    if (!editor) return;
    const QByteArray font = p.fontFamily.toUtf8();
    for (int style = 0; style <= STYLE_MAX; ++style) {
        editor->send(SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(font.constData()));
        editor->send(SCI_STYLESETSIZE, style, p.fontSize);
    }
    editor->send(SCI_SETTABWIDTH, p.tabWidth);
    editor->send(SCI_SETINDENT, p.tabWidth);
    editor->send(SCI_SETUSETABS, p.useTabs);
    editor->send(SCI_SETWRAPMODE, p.wordWrap ? SC_WRAP_WORD : SC_WRAP_NONE);
    editor->send(SCI_SETMARGINWIDTHN, 0, p.lineNumbers ? 20 : 0);
    editor->send(SCI_SETVIEWWS, p.showWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    editor->send(SCI_SETVIEWEOL, p.showWhitespace);
    editor->send(SCI_SETINDENTATIONGUIDES, p.indentGuides ? SC_IV_LOOKFORWARD : SC_IV_NONE);
}
