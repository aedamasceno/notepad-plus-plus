#pragma once

#include "editorpreferences.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QSpinBox;

class PreferencesDialog final : public QDialog
{
    Q_OBJECT
public:
    PreferencesDialog(const EditorPreferences &preferences,
                      std::function<void(const EditorPreferences &)> apply,
                      QWidget *parent = nullptr);
    void setPreferences(const EditorPreferences &preferences);

private:
    EditorPreferences values() const;
    QFontComboBox *m_fontFamily;
    QSpinBox *m_fontSize;
    QSpinBox *m_tabWidth;
    QCheckBox *m_useTabs;
    QCheckBox *m_wordWrap;
    QCheckBox *m_lineNumbers;
    QCheckBox *m_showWhitespace;
    QCheckBox *m_indentGuides;
    QComboBox *m_defaultEol;
    QComboBox *m_defaultEncoding;
    QCheckBox *m_toolbarVisible;
    QCheckBox *m_statusBarVisible;
    QCheckBox *m_rememberWindowState;
    std::function<void(const EditorPreferences &)> m_apply;
};
