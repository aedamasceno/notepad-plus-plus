#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QByteArray>
#include <QHash>
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
    SnapshotMissing
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

class SessionManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int SchemaVersion = 2;
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
    void flush();
    void shutdown();

    QVector<RecoveryDocument> documents() const;
    QString activeDocumentId() const;
    QByteArray readRecoveryContent(const RecoveryDocument &document) const;
    QStringList diagnostics() const;
    QString storageDirectory() const;
    QString sessionFilePath() const;
    bool writesBlocked() const;

private:
    QString defaultStorageDirectory() const;
    QStringList defaultLegacyDirectories() const;
    QString absoluteSnapshotPath(const RecoveryDocument &document) const;
    QString relativeSnapshotPath(const QString &id) const;
    int indexOf(const QString &id) const;
    void captureOriginalMetadata(RecoveryDocument &document);
    void assessRecoveryState(RecoveryDocument &document);
    bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error = nullptr) const;
    void scheduleCheckpoint();
    void writeCheckpoint();
    bool loadVersion2(const QJsonObject &root);
    bool importLegacyDirectory(const QString &directory);

    QTimer m_checkpointTimer;
    QString m_storageDirectory;
    QString m_sessionFilePath;
    QStringList m_legacyDirectories;
    QVector<RecoveryDocument> m_documents;
    QString m_activeDocumentId;
    QStringList m_diagnostics;
    QHash<QString, QByteArray> m_pendingSnapshots;
    QStringList m_obsoleteSnapshots;
    bool m_metadataDirty = false;
    bool m_loaded = false;
    bool m_writesBlocked = false;
};

#endif // SESSIONMANAGER_H
