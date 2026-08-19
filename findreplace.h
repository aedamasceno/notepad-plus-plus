#ifndef FINDREPLACE_H
#define FINDREPLACE_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>

class FindReplaceDialog : public QDialog {
    Q_OBJECT

public:
    explicit FindReplaceDialog(QWidget *parent = nullptr);
    
    QString findText() const;
    QString replaceText() const;
    bool matchCase() const;
    bool wholeWord() const;
    bool wrapAround() const;
    bool isReplaceMode() const;

public slots:
    void setFindText(const QString &text);
    void showReplace();
    void showFind();

signals:
    void findNext();
    void findPrevious();
    void replace();
    void replaceAll();
    void closed();

private slots:
    void onFindNext();
    void onFindPrevious();
    void onReplace();
    void onReplaceAll();
    void onCancel();
    void onTextChanged(const QString &text);

private:
    void setupUI();
    void connectSignals();
    
    // UI elements
    QLineEdit *findLineEdit;
    QLineEdit *replaceLineEdit;
    QPushButton *findNextButton;
    QPushButton *findPrevButton;
    QPushButton *replaceButton;
    QPushButton *replaceAllButton;
    QPushButton *cancelButton;
    QCheckBox *matchCaseCheckBox;
    QCheckBox *wholeWordCheckBox;
    QCheckBox *wrapAroundCheckBox;
    
    // Layouts
    QVBoxLayout *mainLayout;
    QHBoxLayout *buttonLayout;
    QGridLayout *inputLayout;
    
    bool replaceMode;
};

#endif // FINDREPLACE_H
