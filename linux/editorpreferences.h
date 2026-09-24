#pragma once

#include "documentformat.h"

#include <QString>

class QSettings;
class ScintillaEditBase;

struct EditorPreferences {
    QString fontFamily = QStringLiteral("DejaVu Sans Mono");
    int fontSize = 10;
    int tabWidth = 4;
    bool useTabs = false;
    bool wordWrap = false;
    bool lineNumbers = true;
    bool showWhitespace = false;
    bool indentGuides = false;
    DocumentFormat::EolKind defaultEol = DocumentFormat::EolKind::Lf;
    DocumentFormat::TextEncoding defaultEncoding = DocumentFormat::TextEncoding::Utf8;
    bool toolbarVisible = true;
    bool statusBarVisible = true;
    bool rememberWindowState = true;

    bool operator==(const EditorPreferences &other) const;
};

namespace EditorPreferencesStore {
EditorPreferences load(const QSettings &settings);
void save(QSettings &settings, const EditorPreferences &preferences);
void apply(ScintillaEditBase *editor, const EditorPreferences &preferences);
}
