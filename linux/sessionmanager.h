#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QJsonObject;

enum class RecoveryState {
    Ready,
    OriginalMissing,
    OriginalChanged,
    SnapshotMissing,
    SnapshotUnreadable
};

struct RecoveryDocument {
    QString id;
    QString filePath;
    int untitledNumber = 0;
    bool dirty = false;
    QString snapshotPath;
    bool originalExisted = false;
    qint64 originalSize = -1;
    qint64 originalMtimeMs = -1;
    QByteArray originalSha256;
    RecoveryState recoveryState = RecoveryState::Ready;
};

struct DocumentCheckpoint {
    QString id;
    QString filePath;
    int untitledNumber = 0;
    bool dirty = false;
};

struct RecoveryReadResult {
    bool success = false;
    QByteArray content;
    QString error;
};

enum class CheckpointStatus {
    Durable,
    NoChanges,
    WritesBlocked,
    SnapshotWriteFailed,
    MetadataWriteFailed
};

class SessionManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int SchemaVersion = 3;
    static constexpr int DefaultCheckpointIntervalMs = 1000;

    explicit SessionManager(QObject *parent = nullptr,
                            const QString &storageDirectory = QString(),
                            int checkpointIntervalMs = DefaultCheckpointIntervalMs,
                            const QStringList &legacyDirectories = {});
    ~SessionManager() override;

    void loadSession();
    void updateDocument(const DocumentCheckpoint &document, const QByteArray &content);
    void removeDocument(const QString &id);
    void setSessionLayout(const QStringList &orderedIds, const QString &activeId);
    void setDualViewLayout(const QStringList &primaryIds, const QStringList &secondaryIds,
                           const QString &activePane, Qt::Orientation orientation,
                           const QList<int> &splitterSizes);
    CheckpointStatus flush();
    CheckpointStatus shutdown();

    QVector<RecoveryDocument> documents() const;
    QString activeDocumentId() const;
    QStringList primaryDocumentIds() const { return m_primaryDocumentIds; }
    QStringList secondaryDocumentIds() const { return m_secondaryDocumentIds; }
    QString activePane() const { return m_activePane; }
    Qt::Orientation splitterOrientation() const { return m_splitterOrientation; }
    QList<int> splitterSizes() const { return m_splitterSizes; }
    RecoveryReadResult readRecoveryContent(const RecoveryDocument &document) const;
    QStringList diagnostics() const;
    QString storageDirectory() const;
    QString sessionFilePath() const;
    bool writesBlocked() const;

signals:
    void checkpointFailed(const QString &message);

private:
    QString defaultStorageDirectory() const;
    QStringList defaultLegacyDirectories() const;
    QString absoluteSnapshotPath(const RecoveryDocument &document) const;
    QString relativeSnapshotPath(const QString &id) const;
    QString relativeSnapshotPath(const QString &id, const QByteArray &content) const;
    bool isManagedSnapshotPath(const QString &id, const QString &path) const;
    int indexOf(const QString &id) const;
    void captureOriginalMetadata(RecoveryDocument &document);
    void assessRecoveryState(RecoveryDocument &document);
    bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error = nullptr) const;
    void scheduleCheckpoint();
    CheckpointStatus writeCheckpoint();
    bool loadVersion2(const QJsonObject &root);
    bool loadVersion3(const QJsonObject &root);
    bool importLegacyDirectory(const QString &directory);

    QTimer m_checkpointTimer;
    QString m_storageDirectory;
    QString m_sessionFilePath;
    QStringList m_legacyDirectories;
    QVector<RecoveryDocument> m_documents;
    QString m_activeDocumentId;
    QStringList m_primaryDocumentIds;
    QStringList m_secondaryDocumentIds;
    QString m_activePane = QStringLiteral("primary");
    Qt::Orientation m_splitterOrientation = Qt::Horizontal;
    QList<int> m_splitterSizes;
    QStringList m_diagnostics;
    QHash<QString, QByteArray> m_pendingSnapshots;
    QStringList m_obsoleteSnapshots;
    bool m_metadataDirty = false;
    bool m_loaded = false;
    bool m_writesBlocked = false;
};

#endif // SESSIONMANAGER_H
