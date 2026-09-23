#pragma once
#include "searchmanager.h"
#include <QDialog>
class QCheckBox; class QComboBox; class QLineEdit; class QTabWidget; class QPushButton;
class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    enum Page { FindPage, ReplacePage, FilesPage, MarkPage };
    explicit FindReplaceDialog(QWidget *parent = nullptr); ~FindReplaceDialog() override;
    QString findText() const; QString replaceText() const; bool matchCase() const;
    bool wholeWord() const; bool wrapAround() const; bool inSelection() const;
    SearchMode searchMode() const; QString directory() const; QString filters() const;
    bool recursive() const; bool isReplaceMode() const; int currentPage() const;
    void setFileSearchRunning(bool running);
public slots:
    void setFindText(const QString &); void setDirectory(const QString &);
    void showFind(); void showReplace(); void showFindInFiles(); void showMark();
    void rememberInputs();
signals:
    void findNext(); void findPrevious(); void replace(); void replaceAll();
    void countRequested(); void findAllCurrentRequested(); void findAllOpenRequested();
    void findInFilesRequested(); void markAllRequested(); void clearMarksRequested(); void closed();
protected: void closeEvent(QCloseEvent *) override;
private:
    void showPage(Page); void loadSettings(); void saveSettings() const;
    static void addHistory(QComboBox *, const QString &);
    QTabWidget *m_pages; QComboBox *m_find; QComboBox *m_replace; QComboBox *m_mode;
    QCheckBox *m_matchCase; QCheckBox *m_wholeWord; QCheckBox *m_wrap; QCheckBox *m_selection;
    QLineEdit *m_directory; QComboBox *m_filters; QCheckBox *m_recursive;
    QPushButton *m_fileSearchButton = nullptr;
    bool m_fileSearchRunning = false;
};
