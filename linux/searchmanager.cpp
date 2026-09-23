#include "searchmanager.h"

#include "documentformat.h"
#include "editorutils.h"
#include "ScintillaEditBase.h"
#include <forward_list>
#include "ScintillaTypes.h"
#include "ILoader.h"
#include "ILexer.h"
#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"

#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <regex>

namespace {
using Position = Scintilla::Position;

Position documentLength(ScintillaEditBase *editor) {
    return editor ? editor->send(SCI_GETTEXTLENGTH) : 0;
}

SearchRange normalizedRange(ScintillaEditBase *editor, SearchRange range,
                            SearchDirection direction) {
    const qint64 length = documentLength(editor);
    if (range.end < 0)
        range.end = direction == SearchDirection::Forward ? length : 0;
    range.start = qBound<qint64>(0, range.start, length);
    range.end = qBound<qint64>(0, range.end, length);
    return range;
}

SearchRequest forwardRequest(SearchRequest request, qint64 start, qint64 end) {
    request.direction = SearchDirection::Forward;
    request.range = {start, end};
    return request;
}

QByteArray contentRevision(const QByteArray &content) {
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256);
}

class LineMetadataTracker {
public:
    explicit LineMetadataTracker(const QByteArray &content, SearchWorkStats *stats = nullptr)
        : m_content(content), m_stats(stats) {}

    void setLine(qint64 lineStart, qint64 lineEnd) {
        if (lineStart == m_lineStart) return;
        m_lineStart = lineStart;
        m_nextByte = lineStart;
        m_column = 1;
        const qsizetype previewLength = qMin<qint64>(qMax<qint64>(0, lineEnd - lineStart), 500);
        QByteArray line = m_content.mid(lineStart, previewLength);
        if (line.endsWith('\r')) line.chop(1);
        m_preview = QString::fromUtf8(line);
        if (m_stats) m_stats->previewBytesCopied += previewLength;
    }

    int column(qint64 position) {
        if (position < m_nextByte) {
            m_nextByte = m_lineStart;
            m_column = 1;
        }
        const qsizetype byteCount = qMax<qint64>(0, position - m_nextByte);
        if (byteCount) {
            m_column += QString::fromUtf8(m_content.constData() + m_nextByte, byteCount).toUcs4().size();
            m_nextByte = position;
            if (m_stats) m_stats->columnBytesDecoded += byteCount;
        }
        return m_column;
    }

    const QString &preview() const { return m_preview; }

private:
    const QByteArray &m_content;
    SearchWorkStats *m_stats = nullptr;
    qint64 m_lineStart = -1;
    qint64 m_nextByte = 0;
    int m_column = 1;
    QString m_preview;
};

QVariantMap zeroState(const SearchRequest &request, qint64 position,
                      const QByteArray &revision) {
    return {{QStringLiteral("pattern"), request.pattern},
            {QStringLiteral("mode"), int(request.mode)},
            {QStringLiteral("case"), request.options.matchCase},
            {QStringLiteral("word"), request.options.wholeWord},
            {QStringLiteral("direction"), int(request.direction)},
            {QStringLiteral("rangeStart"), request.range.start},
            {QStringLiteral("rangeEnd"), request.range.end},
            {QStringLiteral("position"), position},
            {QStringLiteral("revision"), revision}};
}
}

std::optional<QByteArray> SearchManager::compilePattern(const QString &pattern, SearchMode mode,
                                                        QString *error) {
    if (error) error->clear();
    if (mode != SearchMode::Extended)
        return pattern.toUtf8();
    QByteArray result;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar ch = pattern.at(i);
        if (ch != u'\\') {
            result += QString(ch).toUtf8();
            continue;
        }
        if (++i >= pattern.size()) {
            if (error) *error = QStringLiteral("Trailing backslash in extended pattern");
            return std::nullopt;
        }
        const QChar escaped = pattern.at(i);
        if (escaped == u'n') result += '\n';
        else if (escaped == u'r') result += '\r';
        else if (escaped == u't') result += '\t';
        else if (escaped == u'\\') result += '\\';
        else if (escaped == u'x') {
            if (i + 2 >= pattern.size()) {
                if (error) *error = QStringLiteral("Extended \\x escape requires two hex digits");
                return std::nullopt;
            }
            bool ok = false;
            const int value = pattern.mid(i + 1, 2).toInt(&ok, 16);
            if (!ok) {
                if (error) *error = QStringLiteral("Malformed extended hexadecimal escape");
                return std::nullopt;
            }
            result += char(value); i += 2;
        } else {
            if (error) *error = QStringLiteral("Unknown extended escape \\%1").arg(escaped);
            return std::nullopt;
        }
    }
    return result;
}

int SearchManager::flags(const SearchRequest &request) {
    int value = 0;
    if (request.options.matchCase) value |= SCFIND_MATCHCASE;
    if (request.options.wholeWord) value |= SCFIND_WHOLEWORD;
    if (request.mode == SearchMode::Regex) value |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
    return value;
}

std::optional<SearchMatch> SearchManager::find(ScintillaEditBase *editor,
                                               const SearchRequest &request,
                                               QString *error) const {
    if (!editor) return std::nullopt;
    auto bytes = compilePattern(request.pattern, request.mode, error);
    if (!bytes || (bytes->isEmpty() && request.mode != SearchMode::Regex)) return std::nullopt;
    const SearchRange range = normalizedRange(editor, request.range, request.direction);
    const Position start = range.start;
    const Position end = range.end;
    if ((request.direction == SearchDirection::Forward && start > end) ||
        (request.direction == SearchDirection::Backward && start < end)) return std::nullopt;
    editor->send(SCI_SETSEARCHFLAGS, flags(request));
    editor->send(SCI_SETTARGETSTART, start);
    editor->send(SCI_SETTARGETEND, end);
    editor->send(SCI_SETSTATUS, SC_STATUS_OK);
    const Position found = editor->send(SCI_SEARCHINTARGET, bytes->size(),
                                         reinterpret_cast<sptr_t>(bytes->constData()));
    const int status = editor->send(SCI_GETSTATUS);
    editor->send(SCI_SETSTATUS, SC_STATUS_OK);
    if (status == SC_STATUS_WARN_REGEX) {
        if (error) *error = QStringLiteral("Invalid C++11 regular expression");
        return std::nullopt;
    }
    if (found < 0) return std::nullopt;
    const Position targetStart = editor->send(SCI_GETTARGETSTART);
    const Position targetEnd = editor->send(SCI_GETTARGETEND);
    return SearchMatch{targetStart, targetEnd - targetStart, false};
}

std::optional<SearchMatch> SearchManager::findNext(ScintillaEditBase *editor,
                                                   const SearchRequest &request,
                                                   qint64 position, bool wrap,
                                                   QString *error) const {
    const qint64 length = documentLength(editor);
    if (!editor) return std::nullopt;
    const QByteArray revision = contentRevision(EditorUtils::text(editor));
    const QVariantMap currentState = zeroState(request, position, revision);
    const QVariantMap priorZero = editor->property("nppSearchZeroState").toMap();
    if (!priorZero.isEmpty() && priorZero == currentState) {
        if (request.direction == SearchDirection::Forward && position < length)
            position = editor->send(SCI_POSITIONAFTER, position);
        else if (request.direction == SearchDirection::Backward && position > 0)
            position = editor->send(SCI_POSITIONBEFORE, position);
    }
    const SearchRange bounded = normalizedRange(editor, request.range, SearchDirection::Forward);
    const qint64 low = qMin(bounded.start, bounded.end);
    const qint64 high = qMax(bounded.start, bounded.end);
    const qint64 start = qBound(low, position, high);
    SearchRequest pass = request;
    if (request.direction == SearchDirection::Forward)
        pass.range = {start, high};
    else
        pass.range = {start, low};
    auto hit = find(editor, pass, error);
    if (hit || !wrap) {
        editor->setProperty("nppSearchZeroState", hit && hit->length == 0
            ? QVariant(zeroState(request, hit->start, revision)) : QVariant());
        return hit;
    }
    if (request.direction == SearchDirection::Forward) pass.range = {low, start};
    else pass.range = {high, start};
    hit = find(editor, pass, error);
    if (hit) hit->wrapped = true;
    editor->setProperty("nppSearchZeroState", hit && hit->length == 0
        ? QVariant(zeroState(request, hit->start, revision)) : QVariant());
    return hit;
}

bool SearchManager::selectionMatches(ScintillaEditBase *editor, const SearchRequest &request,
                                     QString *error) const {
    if (!editor) return false;
    const Position start = editor->send(SCI_GETSELECTIONSTART);
    const Position end = editor->send(SCI_GETSELECTIONEND);
    if (start == end && request.mode != SearchMode::Regex) return false;
    SearchRequest exact = request;
    exact.direction = SearchDirection::Forward;
    exact.range = {start, end};
    const auto match = find(editor, exact, error);
    return match && match->start == start && match->end() == end;
}

bool SearchManager::replaceCurrent(ScintillaEditBase *editor, const SearchRequest &request,
                                   const QString &replacement, QString *error) const {
    if (!selectionMatches(editor, request, error)) return false;
    const auto compiledReplacement = request.mode == SearchMode::Extended
        ? compilePattern(replacement, SearchMode::Extended, error)
        : std::optional<QByteArray>(replacement.toUtf8());
    if (!compiledReplacement) return false;
    const QByteArray &bytes = *compiledReplacement;
    const Position start = editor->send(SCI_GETSELECTIONSTART);
    const Position end = editor->send(SCI_GETSELECTIONEND);
    editor->send(SCI_SETTARGETSTART, start); editor->send(SCI_SETTARGETEND, end);
    const unsigned int message = request.mode == SearchMode::Regex ? SCI_REPLACETARGETRE : SCI_REPLACETARGET;
    const Position inserted = editor->send(message, bytes.size(), reinterpret_cast<sptr_t>(bytes.constData()));
    editor->send(SCI_SETSEL, start, start + inserted);
    return true;
}

std::optional<SearchMatch> SearchManager::replaceNext(ScintillaEditBase *editor,
                                                      const SearchRequest &request,
                                                      const QString &replacement, bool wrap,
                                                      QString *error) const {
    if (!editor) return std::nullopt;
    SearchRequest adjusted = request;
    if (selectionMatches(editor, request, error)) {
        const qint64 oldLength = editor->send(SCI_GETSELECTIONEND) -
                                 editor->send(SCI_GETSELECTIONSTART);
        if (!replaceCurrent(editor, request, replacement, error)) return std::nullopt;
        const qint64 newLength = editor->send(SCI_GETSELECTIONEND) -
                                 editor->send(SCI_GETSELECTIONSTART);
        if (adjusted.range.end >= adjusted.range.start)
            adjusted.range.end += newLength - oldLength;
        else
            adjusted.range.start += newLength - oldLength;
    }
    const qint64 position = request.direction == SearchDirection::Forward
        ? editor->send(SCI_GETSELECTIONEND) : editor->send(SCI_GETSELECTIONSTART);
    auto hit = findNext(editor, adjusted, position, wrap, error);
    if (hit) editor->send(SCI_SETSEL, hit->start, hit->end());
    return hit;
}

ReplaceAllResult SearchManager::replaceAll(ScintillaEditBase *editor,
                                           const SearchRequest &request,
                                           const QString &replacement) const {
    ReplaceAllResult result;
    auto pattern = compilePattern(request.pattern, request.mode, &result.error);
    if (!editor || !pattern || (pattern->isEmpty() && request.mode != SearchMode::Regex)) return result;
    SearchRange range = normalizedRange(editor, request.range, SearchDirection::Forward);
    qint64 cursor = qMin(range.start, range.end);
    qint64 boundary = qMax(range.start, range.end);
    const auto compiledReplacement = request.mode == SearchMode::Extended
        ? compilePattern(replacement, SearchMode::Extended, &result.error)
        : std::optional<QByteArray>(replacement.toUtf8());
    if (!compiledReplacement) return result;
    const QByteArray &replacementBytes = *compiledReplacement;
    bool undoStarted = false;
    while (cursor <= boundary) {
        SearchRequest pass = request;
        pass.direction = SearchDirection::Forward; pass.range = {cursor, boundary};
        auto hit = find(editor, pass, &result.error);
        if (!hit) break;
        if (!undoStarted) { editor->send(SCI_BEGINUNDOACTION); undoStarted = true; }
        editor->send(SCI_SETTARGETSTART, hit->start); editor->send(SCI_SETTARGETEND, hit->end());
        const unsigned int message = request.mode == SearchMode::Regex ? SCI_REPLACETARGETRE : SCI_REPLACETARGET;
        const Position inserted = editor->send(message, replacementBytes.size(),
                                                reinterpret_cast<sptr_t>(replacementBytes.constData()));
        ++result.count;
        boundary += inserted - hit->length;
        if (hit->length == 0) {
            if (hit->start + inserted >= boundary) break;
            cursor = editor->send(SCI_POSITIONAFTER, hit->start + inserted);
        } else {
            cursor = hit->start + inserted;
        }
    }
    if (undoStarted) editor->send(SCI_ENDUNDOACTION);
    return result;
}

QVector<SearchResultItem> SearchManager::findAll(ScintillaEditBase *editor,
                                                 const SearchRequest &request,
                                                 const QString &documentId,
                                                 const QString &name, const QString &path,
                                                 QString *error) const {
    QVector<SearchResultItem> results;
    if (!editor) return results;
    const QByteArray content = EditorUtils::text(editor);
    if (m_workStats) ++m_workStats->contentSnapshots;
    const QByteArray revision = contentRevision(content);
    if (m_workStats) ++m_workStats->revisionHashes;
    LineMetadataTracker metadata(content, m_workStats);
    walkMatches(editor, request, [&](const SearchMatch &hit) {
        const int lineIndex = editor->send(SCI_LINEFROMPOSITION, hit.start);
        const Position lineStart = editor->send(SCI_POSITIONFROMLINE, lineIndex);
        const Position lineEnd = editor->send(SCI_GETLINEENDPOSITION, lineIndex);
        metadata.setLine(lineStart, lineEnd);
        results.push_back({documentId, path, name, lineIndex + 1,
                           metadata.column(hit.start), hit.start, hit.length,
                           content.mid(hit.start, hit.length), revision, false,
                           metadata.preview()});
        if (m_workStats) ++m_workStats->resultItems;
    }, error);
    return results;
}

int SearchManager::count(ScintillaEditBase *editor, const SearchRequest &request,
                         QString *error) const {
    return walkMatches(editor, request, {}, error);
}

QVector<SearchResultItem> SearchManager::findAllOpen(const QVector<OpenSearchDocument> &documents,
                                                     const SearchRequest &request,
                                                     QString *error) const {
    QVector<SearchResultItem> results;
    QSet<QString> seen;
    for (const auto &document : documents) {
        if (!document.editor || document.id.isEmpty() || seen.contains(document.id)) continue;
        seen.insert(document.id);
        SearchRequest perDocument = request;
        perDocument.range = {0, documentLength(document.editor)};
        auto found = findAll(document.editor, perDocument, document.id, document.name, document.path, error);
        results += found;
    }
    return results;
}

int SearchManager::markAll(ScintillaEditBase *editor, const SearchRequest &request,
                           QString *error) const {
    if (!editor) return 0;
    clearMarks(editor);
    editor->send(SCI_INDICSETSTYLE, Indicator, INDIC_ROUNDBOX);
    editor->send(SCI_INDICSETFORE, Indicator, 0x00A5FF);
    editor->send(SCI_SETINDICATORCURRENT, Indicator);
    return walkMatches(editor, request, [editor](const SearchMatch &match) {
        if (match.length > 0)
            editor->send(SCI_INDICATORFILLRANGE, match.start, match.length);
    }, error);
}

int SearchManager::walkMatches(ScintillaEditBase *editor, const SearchRequest &request,
                               const std::function<void(const SearchMatch &)> &visitor,
                               QString *error) const {
    if (!editor) return 0;
    auto pattern = compilePattern(request.pattern, request.mode, error);
    if (!pattern || (pattern->isEmpty() && request.mode != SearchMode::Regex)) return 0;
    const SearchRange range = normalizedRange(editor, request.range, SearchDirection::Forward);
    qint64 cursor = qMin(range.start, range.end);
    const qint64 boundary = qMax(range.start, range.end);
    int count = 0;
    editor->send(SCI_SETSEARCHFLAGS, flags(request));
    while (cursor <= boundary) {
        editor->send(SCI_SETTARGETSTART, cursor);
        editor->send(SCI_SETTARGETEND, boundary);
        editor->send(SCI_SETSTATUS, SC_STATUS_OK);
        const Position found = editor->send(SCI_SEARCHINTARGET, pattern->size(),
            reinterpret_cast<sptr_t>(pattern->constData()));
        const int status = editor->send(SCI_GETSTATUS);
        editor->send(SCI_SETSTATUS, SC_STATUS_OK);
        if (status == SC_STATUS_WARN_REGEX) {
            if (error) *error = QStringLiteral("Invalid C++11 regular expression");
            return 0;
        }
        if (found < 0) break;
        const Position start = editor->send(SCI_GETTARGETSTART);
        const Position end = editor->send(SCI_GETTARGETEND);
        const SearchMatch match{start, end - start, false};
        ++count;
        if (visitor) visitor(match);
        if (match.length == 0) {
            if (match.start >= boundary) break;
            cursor = editor->send(SCI_POSITIONAFTER, match.start);
        } else {
            cursor = match.end();
        }
    }
    return count;
}

void SearchManager::clearMarks(ScintillaEditBase *editor) const {
    if (!editor) return;
    editor->send(SCI_SETINDICATORCURRENT, Indicator);
    editor->send(SCI_INDICATORCLEARRANGE, 0, documentLength(editor));
}

FileSearchResult SearchManager::findInFiles(const FileSearchRequest &request) const {
    FileSearchResult result;
    QString patternError;
    auto pattern = compilePattern(request.pattern, request.mode, &patternError);
    if (!pattern || (pattern->isEmpty() && request.mode != SearchMode::Regex)) {
        if (!patternError.isEmpty()) result.errors << patternError;
        return result;
    }
    std::optional<std::wregex> regex;
    if (request.mode == SearchMode::Regex) {
        try {
            auto regexFlags = std::regex::ECMAScript;
            if (!request.options.matchCase) regexFlags |= std::regex::icase;
            regex.emplace(request.pattern.toStdWString(), regexFlags);
        } catch (const std::regex_error &) {
            result.errors << QStringLiteral("Invalid C++11 regular expression");
            return result;
        }
    }
    QDir root(request.directory);
    if (!root.exists()) { result.errors << QStringLiteral("Directory does not exist: %1").arg(request.directory); return result; }
    QStringList filters = request.filters.split(QRegularExpression(QStringLiteral("[;\\s]+")), Qt::SkipEmptyParts);
    if (filters.isEmpty()) filters << QStringLiteral("*");
    QDirIterator iterator(root.absolutePath(), filters, QDir::Files | QDir::NoSymLinks,
                          request.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    int files = 0;
    while (iterator.hasNext()) {
        if (request.cancellation && request.cancellation->load()) {
            result.cancelled = true;
            break;
        }
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (info.isSymLink()) continue;
        if (++files > qMax(1, request.maximumFiles)) { result.limited = true; break; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { ++result.skippedUnreadable; result.errors << path + QStringLiteral(": ") + file.errorString(); continue; }
        const qint64 byteLimit = qMax<qint64>(1, request.maximumFileBytes);
        if (file.size() > byteLimit) {
            ++result.skippedUnreadable;
            result.errors << path + QStringLiteral(": file exceeds search size limit");
            continue;
        }
        if (result.bytesScanned + file.size() > qMax<qint64>(1, request.maximumTotalBytes)) {
            result.limited = true;
            break;
        }
        const QByteArray raw = file.read(byteLimit + 1);
        if (file.error() != QFileDevice::NoError) {
            ++result.skippedUnreadable;
            result.errors << path + QStringLiteral(": ") + file.errorString();
            continue;
        }
        if (raw.size() > byteLimit || (!file.atEnd() && raw.size() == byteLimit + 1)) {
            ++result.skippedUnreadable;
            result.errors << path + QStringLiteral(": file exceeds search size limit");
            continue;
        }
        result.bytesScanned += raw.size();
        ++result.filesScanned;
        if (request.progress) request.progress(result.filesScanned, result.bytesScanned);
        if (raw.contains('\0') && !raw.startsWith("\xFF\xFE") && !raw.startsWith("\xFE\xFF")) {
            ++result.skippedBinary; continue;
        }
        const auto decoded = DocumentFormat::decode(raw);
        if (!decoded.success) { ++result.skippedUnreadable; result.errors << path + QStringLiteral(": ") + decoded.error; continue; }
        const QByteArray revision = contentRevision(decoded.utf8);
        Scintilla::Internal::Document document(Scintilla::DocumentOption::Default);
        document.SetDBCSCodePage(SC_CP_UTF8);
        document.SetCaseFolder(std::make_unique<Scintilla::Internal::CaseFolderUnicode>());
        document.InsertString(0, decoded.utf8.constData(), decoded.utf8.size());
        LineMetadataTracker metadata(decoded.utf8);
        SearchRequest fileRequest;
        fileRequest.mode = request.mode;
        fileRequest.options = request.options;
        const auto findFlags = static_cast<Scintilla::FindOption>(flags(fileRequest));
        const auto appendMatch = [&](qint64 start, qint64 length) {
            const int lineIndex = document.SciLineFromPosition(start);
            const qint64 lineStart = document.LineStart(lineIndex);
            const qint64 lineEnd = document.LineEnd(lineIndex);
            metadata.setLine(lineStart, lineEnd);
            const QByteArray matched = decoded.utf8.mid(start, length);
            result.matches.push_back({info.canonicalFilePath(), info.absoluteFilePath(), info.fileName(),
                                      lineIndex + 1, metadata.column(start),
                                      start, length, matched, revision, true,
                                      metadata.preview()});
        };
        const qint64 boundary = decoded.utf8.size();
        qint64 cursor = 0;
        while (cursor <= boundary) {
            if (request.cancellation && request.cancellation->load()) {
                result.cancelled = true;
                return result;
            }
            qint64 searchEnd = boundary;
            if (request.mode == SearchMode::Regex) {
                const qint64 line = document.SciLineFromPosition(cursor);
                searchEnd = document.LineEnd(line);
                if (cursor > searchEnd) {
                    if (line + 1 >= document.LinesTotal()) break;
                    cursor = document.LineStart(line + 1);
                    continue;
                }
            }
            Sci::Position length = pattern->size();
            qint64 start = -1;
            try {
                start = document.FindText(cursor, searchEnd, pattern->constData(), findFlags, &length);
            } catch (const Scintilla::Internal::RegexError &) {
                result.errors << QStringLiteral("Invalid C++11 regular expression");
                return result;
            }
            if (start < 0) {
                if (request.mode != SearchMode::Regex) break;
                const qint64 line = document.SciLineFromPosition(cursor);
                if (line + 1 >= document.LinesTotal()) break;
                cursor = document.LineStart(line + 1);
                continue;
            }
            appendMatch(start, length);
            if (result.matches.size() >= qMax(1, request.maximumMatches)) {
                result.limited = true;
                return result;
            }
            if (length) {
                cursor = start + length;
            } else {
                if (start >= boundary) break;
                cursor = document.NextPosition(start, 1);
            }
        }
    }
    return result;
}

bool SearchManager::resultStillValid(ScintillaEditBase *editor,
                                     const SearchResultItem &result) {
    if (!editor || result.start < 0 || result.length < 0) return false;
    const QByteArray content = EditorUtils::text(editor);
    if (result.start + result.length > content.size()) return false;
    return result.contentRevision == contentRevision(content) &&
           result.matchedBytes == content.mid(result.start, result.length);
}
