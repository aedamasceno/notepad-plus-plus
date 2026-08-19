#include "findreplace.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>

FindReplaceDialog::FindReplaceDialog(QWidget *parent)
    : QDialog(parent), replaceMode(false) {
    setupUI();
    connectSignals();
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(false);
}

void FindReplaceDialog::setupUI() {
    // Create UI elements
    findLineEdit = new QLineEdit(this);
    replaceLineEdit = new QLineEdit(this);
    findNextButton = new QPushButton("Find Next", this);
    findPrevButton = new QPushButton("Find Previous", this);
    replaceButton = new QPushButton("Replace", this);
    replaceAllButton = new QPushButton("Replace All", this);
    cancelButton = new QPushButton("Cancel", this);
    
    matchCaseCheckBox = new QCheckBox("Match case", this);
    wholeWordCheckBox = new QCheckBox("Whole word", this);
    wrapAroundCheckBox = new QCheckBox("Wrap around", this);
    
    // Set up layout
    mainLayout = new QVBoxLayout(this);
    
    // Input layout
    inputLayout = new QGridLayout();
    inputLayout->addWidget(new QLabel("Find what:"), 0, 0);
    inputLayout->addWidget(findLineEdit, 0, 1);
    inputLayout->addWidget(new QLabel("Replace with:"), 1, 0);
    inputLayout->addWidget(replaceLineEdit, 1, 1);
    
    // Checkboxes
    inputLayout->addWidget(matchCaseCheckBox, 2, 0);
    inputLayout->addWidget(wholeWordCheckBox, 3, 0);
    inputLayout->addWidget(wrapAroundCheckBox, 4, 0);
    
    mainLayout->addLayout(inputLayout);
    
    // Button layout
    buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(findPrevButton);
    buttonLayout->addWidget(findNextButton);
    buttonLayout->addWidget(replaceButton);
    buttonLayout->addWidget(replaceAllButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Set initial state
    replaceButton->setEnabled(false);
    replaceAllButton->setEnabled(false);
    
    setWindowTitle("Find and Replace");
    resize(400, 150);
}

void FindReplaceDialog::connectSignals() {
    connect(findNextButton, &QPushButton::clicked, this, &FindReplaceDialog::onFindNext);
    connect(findPrevButton, &QPushButton::clicked, this, &FindReplaceDialog::onFindPrevious);
    connect(replaceButton, &QPushButton::clicked, this, &FindReplaceDialog::onReplace);
    connect(replaceAllButton, &QPushButton::clicked, this, &FindReplaceDialog::onReplaceAll);
    connect(cancelButton, &QPushButton::clicked, this, &FindReplaceDialog::onCancel);
    connect(findLineEdit, &QLineEdit::textChanged, this, &FindReplaceDialog::onTextChanged);
    connect(replaceLineEdit, &QLineEdit::textChanged, this, &FindReplaceDialog::onTextChanged);
    
    // Connect the dialog signals
    connect(this, &FindReplaceDialog::findNext, this, [this]() { emit findNext(); });
    connect(this, &FindReplaceDialog::findPrevious, this, [this]() { emit findPrevious(); });
    connect(this, &FindReplaceDialog::replace, this, [this]() { emit replace(); });
    connect(this, &FindReplaceDialog::replaceAll, this, [this]() { emit replaceAll(); });
    connect(this, &FindReplaceDialog::closed, this, [this]() { emit closed(); });
}

void FindReplaceDialog::onFindNext() {
    emit findNext();
}

void FindReplaceDialog::onFindPrevious() {
    emit findPrevious();
}

void FindReplaceDialog::onReplace() {
    emit replace();
}

void FindReplaceDialog::onReplaceAll() {
    emit replaceAll();
}

void FindReplaceDialog::onCancel() {
    hide();
    emit closed();
}

void FindReplaceDialog::onTextChanged(const QString &) {
    bool hasText = !findLineEdit->text().isEmpty();
    findNextButton->setEnabled(hasText);
    findPrevButton->setEnabled(hasText);
    replaceButton->setEnabled(hasText);
    replaceAllButton->setEnabled(hasText);
}

QString FindReplaceDialog::findText() const {
    return findLineEdit->text();
}

QString FindReplaceDialog::replaceText() const {
    return replaceLineEdit->text();
}

bool FindReplaceDialog::matchCase() const {
    return matchCaseCheckBox->isChecked();
}

bool FindReplaceDialog::wholeWord() const {
    return wholeWordCheckBox->isChecked();
}

bool FindReplaceDialog::wrapAround() const {
    return wrapAroundCheckBox->isChecked();
}

bool FindReplaceDialog::isReplaceMode() const {
    return replaceMode;
}

void FindReplaceDialog::setFindText(const QString &text) {
    findLineEdit->setText(text);
}

void FindReplaceDialog::showReplace() {
    replaceMode = true;
    replaceLineEdit->setVisible(true);
    replaceButton->setVisible(true);
    replaceAllButton->setVisible(true);
    
    // Adjust layout
    inputLayout->addWidget(new QLabel("Replace with:"), 1, 0);
    inputLayout->addWidget(replaceLineEdit, 1, 1);
    
    setWindowTitle("Find and Replace");
    adjustSize();
}

void FindReplaceDialog::showFind() {
    replaceMode = false;
    replaceLineEdit->setVisible(false);
    replaceButton->setVisible(false);
    replaceAllButton->setVisible(false);
    
    // Adjust layout
    if (inputLayout->count() > 2) {  // Remove replace line if exists
        for (int i = inputLayout->count() - 1; i >= 0; --i) {
            QLayoutItem *item = inputLayout->itemAt(i);
            if (item) {
                QWidget *widget = item->widget();
                if (widget) {
                    if (auto *label = qobject_cast<QLabel *>(widget)) {
                        if (label->text() == "Replace with:") {
                            inputLayout->removeWidget(widget);
                            delete widget;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    setWindowTitle("Find");
    adjustSize();
}

#include "findreplace.moc"
