#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <atomic>
#include <memory>
#include <optional>

class ScintillaEditBase;

enum class SearchMode { Normal, Extended, Regex };
enum class SearchDirection { Forward, Backward };

struct SearchOptions {
    bool matchCase = false;
    bool wholeWord = false;
};

struct SearchRange {
    qint64 start = 0;
    qint64 end = -1;
};

struct SearchRequest {
    QString pattern;
    SearchMode mode = SearchMode::Normal;
    SearchOptions options;
    SearchDirection direction = SearchDirection::Forward;
    SearchRange range;
};

struct SearchMatch {
    qint64 start = -1;
    qint64 length = 0;
    bool wrapped = false;
    qint64 end() const { return start + length; }
};

struct SearchResultItem {
    QString documentId;
    QString path;
    QString name;
    int line = 1;
    int column = 1;
    qint64 start = 0;
    qint64 length = 0;
    QByteArray matchedBytes;
    QByteArray contentRevision;
    bool fromFileSearch = false;
    QString preview;
};

struct ReplaceAllResult { int count = 0; QString error; };

// Optional diagnostic seam used to assert bounded bulk-search work.
struct SearchWorkStats {
    qint64 contentSnapshots = 0;
    qint64 revisionHashes = 0;
    qint64 resultItems = 0;
    qint64 columnBytesDecoded = 0;
    qint64 previewBytesCopied = 0;
};

struct FileSearchRequest {
    QString pattern;
    QString directory;
    QString filters = QStringLiteral("*");
    SearchMode mode = SearchMode::Normal;
    SearchOptions options;
    bool recursive = true;
    int maximumFiles = 10000;
    int maximumMatches = 10000;
    qint64 maximumFileBytes = 16 * 1024 * 1024;
    qint64 maximumTotalBytes = 256 * 1024 * 1024;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::function<void(int, qint64)> progress;
};

struct FileSearchResult {
    QVector<SearchResultItem> matches;
    QStringList errors;
    int skippedBinary = 0;
    int skippedUnreadable = 0;
    int filesScanned = 0;
    qint64 bytesScanned = 0;
    bool limited = false;
    bool cancelled = false;
};

struct OpenSearchDocument {
    QString id;
    QString path;
    QString name;
    ScintillaEditBase *editor = nullptr;
};

class SearchManager {
public:
    static constexpr int Indicator = 22;

    explicit SearchManager(SearchWorkStats *workStats = nullptr) : m_workStats(workStats) {}

    static std::optional<QByteArray> compilePattern(const QString &pattern, SearchMode mode,
                                                    QString *error = nullptr);
    std::optional<SearchMatch> find(ScintillaEditBase *editor, const SearchRequest &request,
                                    QString *error = nullptr) const;
    std::optional<SearchMatch> findNext(ScintillaEditBase *editor, const SearchRequest &request,
                                        qint64 position, bool wrap, QString *error = nullptr) const;
    bool selectionMatches(ScintillaEditBase *editor, const SearchRequest &request,
                          QString *error = nullptr) const;
    bool replaceCurrent(ScintillaEditBase *editor, const SearchRequest &request,
                        const QString &replacement, QString *error = nullptr) const;
    std::optional<SearchMatch> replaceNext(ScintillaEditBase *editor,
                                           const SearchRequest &request,
                                           const QString &replacement, bool wrap,
                                           QString *error = nullptr) const;
    ReplaceAllResult replaceAll(ScintillaEditBase *editor, const SearchRequest &request,
                                const QString &replacement) const;
    int count(ScintillaEditBase *editor, const SearchRequest &request,
              QString *error = nullptr) const;
    QVector<SearchResultItem> findAll(ScintillaEditBase *editor, const SearchRequest &request,
                                      const QString &documentId, const QString &name,
                                      const QString &path, QString *error = nullptr) const;
    QVector<SearchResultItem> findAllOpen(const QVector<OpenSearchDocument> &documents,
                                          const SearchRequest &request,
                                          QString *error = nullptr) const;
    int markAll(ScintillaEditBase *editor, const SearchRequest &request,
                QString *error = nullptr) const;
    void clearMarks(ScintillaEditBase *editor) const;
    FileSearchResult findInFiles(const FileSearchRequest &request) const;
    static bool resultStillValid(ScintillaEditBase *editor, const SearchResultItem &result);

private:
    static int flags(const SearchRequest &request);
    int walkMatches(ScintillaEditBase *editor, const SearchRequest &request,
                    const std::function<void(const SearchMatch &)> &visitor,
                    QString *error) const;
    SearchWorkStats *m_workStats = nullptr;
};
