#include "preferencesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(const EditorPreferences &p,
                                     std::function<void(const EditorPreferences &)> apply,
                                     QWidget *parent)
    : QDialog(parent), m_apply(std::move(apply))
{
    setObjectName(QStringLiteral("preferencesDialog"));
    setWindowTitle(tr("Preferences"));
    auto *layout = new QVBoxLayout(this);
    auto *editorGroup = new QGroupBox(tr("Editor"), this);
    auto *form = new QFormLayout(editorGroup);
    m_fontFamily = new QFontComboBox(editorGroup); m_fontFamily->setObjectName("fontFamilyComboBox");
    m_fontFamily->setCurrentFont(QFont(p.fontFamily));
    m_fontSize = new QSpinBox(editorGroup); m_fontSize->setObjectName("fontSizeSpinBox");
    m_fontSize->setRange(6, 72); m_fontSize->setValue(p.fontSize);
    m_tabWidth = new QSpinBox(editorGroup); m_tabWidth->setObjectName("tabWidthSpinBox");
    m_tabWidth->setRange(1, 16); m_tabWidth->setValue(p.tabWidth);
    m_useTabs = new QCheckBox(tr("Use tabs instead of spaces"), editorGroup); m_useTabs->setObjectName("useTabsCheckBox"); m_useTabs->setChecked(p.useTabs);
    m_wordWrap = new QCheckBox(tr("Word wrap"), editorGroup); m_wordWrap->setObjectName("wordWrapCheckBox"); m_wordWrap->setChecked(p.wordWrap);
    m_lineNumbers = new QCheckBox(tr("Line numbers"), editorGroup); m_lineNumbers->setObjectName("lineNumbersCheckBox"); m_lineNumbers->setChecked(p.lineNumbers);
    m_showWhitespace = new QCheckBox(tr("Show whitespace and end-of-line characters"), editorGroup); m_showWhitespace->setObjectName("showWhitespaceCheckBox"); m_showWhitespace->setChecked(p.showWhitespace);
    m_indentGuides = new QCheckBox(tr("Indent guides"), editorGroup); m_indentGuides->setObjectName("indentGuidesCheckBox"); m_indentGuides->setChecked(p.indentGuides);
    form->addRow(tr("Font:"), m_fontFamily); form->addRow(tr("Size:"), m_fontSize);
    form->addRow(tr("Tab width:"), m_tabWidth); form->addRow(m_useTabs);
    form->addRow(m_wordWrap); form->addRow(m_lineNumbers); form->addRow(m_showWhitespace); form->addRow(m_indentGuides);
    layout->addWidget(editorGroup);

    auto *documentGroup = new QGroupBox(tr("New documents"), this);
    auto *documentForm = new QFormLayout(documentGroup);
    m_defaultEol = new QComboBox(documentGroup); m_defaultEol->setObjectName("defaultEolComboBox");
    m_defaultEol->addItem(tr("Unix (LF)"), int(DocumentFormat::EolKind::Lf));
    m_defaultEol->addItem(tr("Windows (CR LF)"), int(DocumentFormat::EolKind::CrLf));
    m_defaultEol->addItem(tr("Macintosh (CR)"), int(DocumentFormat::EolKind::Cr));
    m_defaultEol->setCurrentIndex(m_defaultEol->findData(int(p.defaultEol)));
    m_defaultEncoding = new QComboBox(documentGroup); m_defaultEncoding->setObjectName("defaultEncodingComboBox");
    m_defaultEncoding->addItem(tr("UTF-8"), int(DocumentFormat::TextEncoding::Utf8));
    m_defaultEncoding->addItem(tr("UTF-8 BOM"), int(DocumentFormat::TextEncoding::Utf8Bom));
    m_defaultEncoding->addItem(tr("UTF-16 LE"), int(DocumentFormat::TextEncoding::Utf16Le));
    m_defaultEncoding->addItem(tr("UTF-16 BE"), int(DocumentFormat::TextEncoding::Utf16Be));
    m_defaultEncoding->addItem(tr("Windows-1252"), int(DocumentFormat::TextEncoding::Windows1252));
    m_defaultEncoding->setCurrentIndex(m_defaultEncoding->findData(int(p.defaultEncoding)));
    documentForm->addRow(tr("Line endings:"), m_defaultEol);
    documentForm->addRow(tr("Encoding:"), m_defaultEncoding);
    layout->addWidget(documentGroup);

    auto *windowGroup = new QGroupBox(tr("Window and session"), this);
    auto *windowLayout = new QVBoxLayout(windowGroup);
    m_toolbarVisible = new QCheckBox(tr("Show toolbar"), windowGroup); m_toolbarVisible->setObjectName("toolbarVisibleCheckBox"); m_toolbarVisible->setChecked(p.toolbarVisible);
    m_statusBarVisible = new QCheckBox(tr("Show status bar"), windowGroup); m_statusBarVisible->setObjectName("statusBarVisibleCheckBox"); m_statusBarVisible->setChecked(p.statusBarVisible);
    m_rememberWindowState = new QCheckBox(tr("Remember window geometry and layout"), windowGroup); m_rememberWindowState->setObjectName("rememberWindowStateCheckBox"); m_rememberWindowState->setChecked(p.rememberWindowState);
    windowLayout->addWidget(m_toolbarVisible);
    windowLayout->addWidget(m_statusBarVisible); windowLayout->addWidget(m_rememberWindowState);
    layout->addWidget(windowGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { m_apply(values()); });
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, [this] { m_apply(values()); accept(); });
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &QDialog::reject);
}

void PreferencesDialog::setPreferences(const EditorPreferences &p)
{
    m_fontFamily->setCurrentFont(QFont(p.fontFamily));
    m_fontSize->setValue(p.fontSize);
    m_tabWidth->setValue(p.tabWidth);
    m_useTabs->setChecked(p.useTabs);
    m_wordWrap->setChecked(p.wordWrap);
    m_lineNumbers->setChecked(p.lineNumbers);
    m_showWhitespace->setChecked(p.showWhitespace);
    m_indentGuides->setChecked(p.indentGuides);
    m_defaultEol->setCurrentIndex(m_defaultEol->findData(int(p.defaultEol)));
    m_defaultEncoding->setCurrentIndex(m_defaultEncoding->findData(int(p.defaultEncoding)));
    m_toolbarVisible->setChecked(p.toolbarVisible);
    m_statusBarVisible->setChecked(p.statusBarVisible);
    m_rememberWindowState->setChecked(p.rememberWindowState);
}

EditorPreferences PreferencesDialog::values() const
{
    EditorPreferences p;
    p.fontFamily = m_fontFamily->currentFont().family(); p.fontSize = m_fontSize->value();
    p.tabWidth = m_tabWidth->value(); p.useTabs = m_useTabs->isChecked();
    p.wordWrap = m_wordWrap->isChecked(); p.lineNumbers = m_lineNumbers->isChecked();
    p.showWhitespace = m_showWhitespace->isChecked(); p.indentGuides = m_indentGuides->isChecked();
    p.defaultEol = DocumentFormat::EolKind(m_defaultEol->currentData().toInt());
    p.defaultEncoding = DocumentFormat::TextEncoding(m_defaultEncoding->currentData().toInt());
    p.toolbarVisible = m_toolbarVisible->isChecked();
    p.statusBarVisible = m_statusBarVisible->isChecked(); p.rememberWindowState = m_rememberWindowState->isChecked();
    return p;
}
