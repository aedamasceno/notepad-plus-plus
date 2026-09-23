#include "findreplace.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace { QPushButton *makeButton(const QString &text, const char *name, QWidget *parent) {
    auto *b = new QPushButton(text, parent); b->setObjectName(QString::fromLatin1(name)); return b;
} }
FindReplaceDialog::FindReplaceDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("findReplaceDialog")); setModal(false); setWindowTitle(tr("Find / Replace"));
    m_find = new QComboBox(this); m_find->setEditable(true); m_find->setObjectName(QStringLiteral("findTextCombo"));
    m_replace = new QComboBox(this); m_replace->setEditable(true); m_replace->setObjectName(QStringLiteral("replaceTextCombo"));
    m_mode = new QComboBox(this); m_mode->setObjectName(QStringLiteral("searchModeCombo"));
    m_mode->addItems({tr("Normal"), tr("Extended"), tr("Regular expression")});
    m_matchCase = new QCheckBox(tr("Match case"), this); m_matchCase->setObjectName(QStringLiteral("matchCaseCheckBox"));
    m_wholeWord = new QCheckBox(tr("Whole word"), this); m_wholeWord->setObjectName(QStringLiteral("wholeWordCheckBox"));
    m_wrap = new QCheckBox(tr("Wrap around"), this); m_wrap->setObjectName(QStringLiteral("wrapCheckBox"));
    m_selection = new QCheckBox(tr("In selection"), this); m_selection->setObjectName(QStringLiteral("inSelectionCheckBox"));
    m_directory = new QLineEdit(this); m_directory->setObjectName(QStringLiteral("findFilesDirectory"));
    m_filters = new QComboBox(this); m_filters->setEditable(true); m_filters->setObjectName(QStringLiteral("findFilesFilters"));
    m_recursive = new QCheckBox(tr("Search subdirectories"), this); m_recursive->setObjectName(QStringLiteral("recursiveCheckBox"));
    m_pages = new QTabWidget(this); m_pages->setObjectName(QStringLiteral("searchPages"));
    auto page = [this](bool) { auto *w = new QWidget(m_pages); new QFormLayout(w); return w; };
    QWidget *fp = page(false); auto *fb = new QHBoxLayout;
    auto *prev = makeButton(tr("Find Previous"), "findPreviousButton", fp); auto *next = makeButton(tr("Find Next"), "findNextButton", fp);
    auto *count = makeButton(tr("Count"), "countButton", fp); auto *current = makeButton(tr("Find All Current"), "findAllCurrentButton", fp);
    auto *open = makeButton(tr("Find All Open"), "findAllOpenButton", fp);
    for (auto *b : {prev, next, count, current, open}) fb->addWidget(b); qobject_cast<QFormLayout *>(fp->layout())->addRow(fb);
    QWidget *rp = page(true); auto *rb = new QHBoxLayout; auto *replace = makeButton(tr("Replace"), "replaceButton", rp);
    auto *replaceAll = makeButton(tr("Replace All"), "replaceAllButton", rp); rb->addWidget(replace); rb->addWidget(replaceAll);
    qobject_cast<QFormLayout *>(rp->layout())->addRow(rb);
    auto *files = new QWidget(m_pages); auto *fl = new QFormLayout(files); fl->addRow(tr("Directory:"), m_directory);
    fl->addRow(tr("Filters:"), m_filters); fl->addRow(m_recursive); m_fileSearchButton = makeButton(tr("Find in Files"), "findInFilesButton", files); fl->addRow(m_fileSearchButton);
    QWidget *mp = page(false); auto *mb = new QHBoxLayout; auto *mark = makeButton(tr("Mark All"), "markAllButton", mp);
    auto *clear = makeButton(tr("Clear Marks"), "clearMarksButton", mp); mb->addWidget(mark); mb->addWidget(clear); qobject_cast<QFormLayout *>(mp->layout())->addRow(mb);
    m_pages->addTab(fp, tr("Find")); m_pages->addTab(rp, tr("Replace")); m_pages->addTab(files, tr("Find in Files")); m_pages->addTab(mp, tr("Mark"));
    auto *opts = new QGridLayout; opts->addWidget(m_matchCase,0,0); opts->addWidget(m_wholeWord,0,1); opts->addWidget(m_wrap,1,0); opts->addWidget(m_selection,1,1);
    opts->addWidget(new QLabel(tr("Search mode:"),this),2,0); opts->addWidget(m_mode,2,1);
    auto *inputs = new QFormLayout; inputs->addRow(tr("Find what:"), m_find); inputs->addRow(tr("Replace with:"), m_replace);
    auto *layout = new QVBoxLayout(this); layout->addLayout(inputs); layout->addWidget(m_pages); layout->addLayout(opts);
    connect(next,&QPushButton::clicked,this,[this]{rememberInputs();emit findNext();}); connect(prev,&QPushButton::clicked,this,[this]{rememberInputs();emit findPrevious();});
    connect(replace,&QPushButton::clicked,this,[this]{rememberInputs();emit this->replace();}); connect(replaceAll,&QPushButton::clicked,this,[this]{rememberInputs();emit this->replaceAll();});
    connect(count,&QPushButton::clicked,this,[this]{rememberInputs();emit countRequested();}); connect(current,&QPushButton::clicked,this,[this]{rememberInputs();emit findAllCurrentRequested();});
    connect(open,&QPushButton::clicked,this,[this]{rememberInputs();emit findAllOpenRequested();}); connect(m_fileSearchButton,&QPushButton::clicked,this,[this]{rememberInputs();emit findInFilesRequested();});
    connect(mark,&QPushButton::clicked,this,[this]{rememberInputs();emit markAllRequested();}); connect(clear,&QPushButton::clicked,this,&FindReplaceDialog::clearMarksRequested);
    const QList<QPushButton *> patternButtons{prev, next, count, current, open, replace,
                                              replaceAll, m_fileSearchButton, mark};
    const auto updatePatternActions = [this, patternButtons] {
        const bool enabled = !m_find->currentText().isEmpty();
        for (auto *button : patternButtons) button->setEnabled(enabled);
        if (m_fileSearchRunning) m_fileSearchButton->setEnabled(false);
    };
    connect(m_find, &QComboBox::editTextChanged, this,
            [updatePatternActions](const QString &) { updatePatternActions(); });
    loadSettings(); updatePatternActions(); resize(680,300);
}
FindReplaceDialog::~FindReplaceDialog(){saveSettings();}
QString FindReplaceDialog::findText()const{return m_find->currentText();} QString FindReplaceDialog::replaceText()const{return m_replace->currentText();}
bool FindReplaceDialog::matchCase()const{return m_matchCase->isChecked();} bool FindReplaceDialog::wholeWord()const{return m_wholeWord->isChecked();}
bool FindReplaceDialog::wrapAround()const{return m_wrap->isChecked();} bool FindReplaceDialog::inSelection()const{return m_selection->isChecked();}
SearchMode FindReplaceDialog::searchMode()const{return static_cast<SearchMode>(m_mode->currentIndex());} QString FindReplaceDialog::directory()const{return m_directory->text();}
QString FindReplaceDialog::filters()const{return m_filters->currentText();} bool FindReplaceDialog::recursive()const{return m_recursive->isChecked();}
bool FindReplaceDialog::isReplaceMode()const{return currentPage()==ReplacePage;} int FindReplaceDialog::currentPage()const{return m_pages->currentIndex();}
void FindReplaceDialog::setFileSearchRunning(bool running){m_fileSearchRunning=running;m_fileSearchButton->setEnabled(!running&&!findText().isEmpty());m_fileSearchButton->setText(running?tr("Searching…"):tr("Find in Files"));}
void FindReplaceDialog::setFindText(const QString&s){m_find->setEditText(s);} void FindReplaceDialog::setDirectory(const QString&s){m_directory->setText(s);}
void FindReplaceDialog::showPage(Page p){m_pages->setCurrentIndex(p);show();raise();activateWindow();m_find->setFocus();}
void FindReplaceDialog::showFind(){showPage(FindPage);} void FindReplaceDialog::showReplace(){showPage(ReplacePage);} void FindReplaceDialog::showFindInFiles(){showPage(FilesPage);} void FindReplaceDialog::showMark(){showPage(MarkPage);}
void FindReplaceDialog::closeEvent(QCloseEvent*e){saveSettings();emit closed();QDialog::closeEvent(e);}
void FindReplaceDialog::addHistory(QComboBox*b,const QString&v){if(v.isEmpty())return;int i=b->findText(v);if(i>=0)b->removeItem(i);b->insertItem(0,v);while(b->count()>30)b->removeItem(b->count()-1);b->setCurrentIndex(0);}
void FindReplaceDialog::rememberInputs(){addHistory(m_find,findText());addHistory(m_replace,replaceText());addHistory(m_filters,filters());saveSettings();}
void FindReplaceDialog::loadSettings(){QSettings s;QString r="search/";m_find->addItems(s.value(r+"findHistory").toStringList());m_replace->addItems(s.value(r+"replaceHistory").toStringList());m_filters->addItems(s.value(r+"filterHistory",QStringList{"*"}).toStringList());m_directory->setText(s.value(r+"directory").toString());m_matchCase->setChecked(s.value(r+"matchCase",false).toBool());m_wholeWord->setChecked(s.value(r+"wholeWord",false).toBool());m_wrap->setChecked(s.value(r+"wrap",true).toBool());m_recursive->setChecked(s.value(r+"recursive",true).toBool());m_mode->setCurrentIndex(qBound(0,s.value(r+"mode",0).toInt(),2));}
void FindReplaceDialog::saveSettings()const{auto vals=[](QComboBox*b){QStringList o;const QString current=b->currentText();if(!current.isEmpty())o<<current;for(int i=0;i<b->count()&&o.size()<30;++i)if(!b->itemText(i).isEmpty()&&!o.contains(b->itemText(i)))o<<b->itemText(i);return o;};QSettings s;QString r="search/";s.setValue(r+"findHistory",vals(m_find));s.setValue(r+"replaceHistory",vals(m_replace));s.setValue(r+"filterHistory",vals(m_filters));s.setValue(r+"directory",directory());s.setValue(r+"matchCase",matchCase());s.setValue(r+"wholeWord",wholeWord());s.setValue(r+"wrap",wrapAround());s.setValue(r+"recursive",recursive());s.setValue(r+"mode",int(searchMode()));}
