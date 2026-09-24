#include "functionlist.h"

#include <QRegularExpression>

QVector<FunctionEntry> parseFunctions(const QString &text, const QString &languageId)
{
    QVector<FunctionEntry> result;
    const bool python = languageId == QStringLiteral("python");
    const bool javascript = languageId == QStringLiteral("javascript");
    const bool braceLanguage = javascript || languageId == QStringLiteral("c") ||
        languageId == QStringLiteral("cpp") || languageId == QStringLiteral("csharp") ||
        languageId == QStringLiteral("java");
    if (!python && !braceLanguage)
        return result;
    const QRegularExpression pythonPattern(
        QStringLiteral(R"(^\s*(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\()"));
    const QRegularExpression bracePattern(
        QStringLiteral(R"(^\s*(?:(?:public|private|protected|static|virtual|inline|constexpr|async|export)\s+)*(?:(?:[A-Za-z_$][\w$:<>,\[\]*&?.]*\s+)+)?([A-Za-z_$][\w$]*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:throws\s+[^{]+)?\{?\s*$)"));
    const QRegularExpression jsFunctionPattern(
        QStringLiteral(R"(^\s*(?:export\s+)?(?:async\s+)?function\s+([A-Za-z_$][\w$]*)\s*\()"));
    const QRegularExpression jsArrowPattern(
        QStringLiteral(R"(^\s*(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=.*=>)"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int line = 0; line < lines.size(); ++line) {
        QRegularExpressionMatch match;
        if (python)
            match = pythonPattern.match(lines.at(line));
        else {
            if (javascript)
                match = jsFunctionPattern.match(lines.at(line));
            if (javascript && !match.hasMatch())
                match = jsArrowPattern.match(lines.at(line));
            if (!match.hasMatch())
                match = bracePattern.match(lines.at(line));
        }
        if (match.hasMatch()) {
            const QString name = match.captured(1);
            static const QStringList controlWords = {
                QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"),
                QStringLiteral("switch"), QStringLiteral("catch")};
            if (!controlWords.contains(name))
                result.push_back({name, line});
        }
    }
    return result;
}

FunctionList::FunctionList(QWidget *parent)
    : QDockWidget(tr("Function List"), parent), m_tree(new QTreeWidget(this))
{
    setObjectName(QStringLiteral("FunctionListDock"));
    m_tree->setObjectName(QStringLiteral("FunctionListView"));
    m_tree->setHeaderLabels({tr("Function"), tr("Line")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    setWidget(m_tree);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit lineActivated(item->data(0, Qt::UserRole).toInt());
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                emit lineActivated(item->data(0, Qt::UserRole).toInt());
            });
}

void FunctionList::setDocument(const QString &text, const QString &languageId)
{
    if (text == m_text && languageId == m_languageId)
        return;
    m_text = text;
    m_languageId = languageId;
    m_tree->clear();
    const auto functions = parseFunctions(text, languageId);
    for (const auto &function : functions) {
        auto *item = new QTreeWidgetItem(m_tree, {function.name, QString::number(function.line + 1)});
        item->setData(0, Qt::UserRole, function.line);
    }
}
