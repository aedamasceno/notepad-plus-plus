#include "sessionmanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSet>
#include <QStandardPaths>
#include <utility>

namespace {
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result();
}

QString stateDiagnostic(const RecoveryDocument &document)
{
    switch (document.recoveryState) {
    case RecoveryState::OriginalMissing:
        return QStringLiteral("Original is missing for recovered document: %1").arg(document.filePath);
    case RecoveryState::OriginalChanged:
        return QStringLiteral("Original changed externally; recovered snapshot was kept separate: %1")
            .arg(document.filePath);
    case RecoveryState::SnapshotMissing:
        return QStringLiteral("Recovery snapshot is missing for document %1").arg(document.id);
    case RecoveryState::SnapshotUnreadable:
        return QStringLiteral("Recovery snapshot is unreadable for document %1").arg(document.id);
    case RecoveryState::Ready:
        return {};
    }
    return {};
}
}

SessionManager::SessionManager(QObject *parent, const QString &storageDirectory,
                               int checkpointIntervalMs, const QStringList &legacyDirectories)
    : QObject(parent),
      m_storageDirectory(storageDirectory.isEmpty() ? defaultStorageDirectory() : storageDirectory),
      m_legacyDirectories(legacyDirectories.isEmpty() ? defaultLegacyDirectories() : legacyDirectories)
{
    m_sessionFilePath = QDir(m_storageDirectory).filePath(QStringLiteral("session.json"));
    m_checkpointTimer.setSingleShot(true);
    m_checkpointTimer.setInterval(qMax(1, checkpointIntervalMs));
    connect(&m_checkpointTimer, &QTimer::timeout, this, &SessionManager::writeCheckpoint);
}

SessionManager::~SessionManager()
{
    const QSignalBlocker blocker(this);
    shutdown();
}

QString SessionManager::defaultStorageDirectory() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("notepad++/sessions"));
}

QStringList SessionManager::defaultLegacyDirectories() const
{
    const QString generic = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList candidates{
        QDir(generic).filePath(QStringLiteral("notepad++/sessions")),
        QDir(generic).filePath(QStringLiteral("Notepad++/notepad++/sessions")),
        QDir(generic).filePath(QStringLiteral("Notepad++/Notepad++ Linux/notepad++/sessions"))
    };
    candidates.removeAll(m_storageDirectory);
    candidates.removeDuplicates();
    return candidates;
}

void SessionManager::loadSession()
{
    if (m_loaded)
        return;
    m_loaded = true;
    QDir().mkpath(QDir(m_storageDirectory).filePath(QStringLiteral("snapshots")));

    if (!QFileInfo::exists(m_sessionFilePath)) {
        for (const QString &legacy : std::as_const(m_legacyDirectories)) {
            if (importLegacyDirectory(legacy)) {
                m_metadataDirty = true;
                break;
            }
        }
        return;
    }

    QFile file(m_sessionFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_diagnostics << QStringLiteral("Could not read session metadata: %1").arg(file.errorString());
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Malformed session metadata retained at %1; recovery writes are disabled: %2")
                             .arg(m_sessionFilePath, parseError.errorString());
        return;
    }
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != SchemaVersion) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Unsupported session schema retained at %1; recovery writes are disabled")
                             .arg(m_sessionFilePath);
        return;
    }
    loadVersion2(root);
}

bool SessionManager::loadVersion2(const QJsonObject &root)
{
    m_activeDocumentId = root.value(QStringLiteral("activeDocumentId")).toString();
    const QJsonValue documentsValue = root.value(QStringLiteral("documents"));
    if (!documentsValue.isArray()) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Session document list is malformed; metadata was retained and recovery writes are disabled");
        return false;
    }

    QSet<QString> seen;
    for (const QJsonValue &value : documentsValue.toArray()) {
        if (!value.isObject()) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Ignored malformed document entry; metadata retained and recovery writes disabled");
            continue;
        }
        const QJsonObject object = value.toObject();
        RecoveryDocument document;
        document.id = object.value(QStringLiteral("id")).toString();
        if (document.id.isEmpty() || seen.contains(document.id)) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Ignored empty or duplicate recovery identity; metadata retained and recovery writes disabled");
            continue;
        }
        seen.insert(document.id);
        document.filePath = object.value(QStringLiteral("filePath")).toString();
        document.untitledNumber = object.value(QStringLiteral("untitledNumber")).toInt();
        document.dirty = object.value(QStringLiteral("dirty")).toBool();
        const QString storedSnapshot = object.value(QStringLiteral("snapshot")).toString();
        const QString expectedSnapshot = relativeSnapshotPath(document.id);
        if (!storedSnapshot.isEmpty() && storedSnapshot != expectedSnapshot) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Rejected unmanaged snapshot path for %1; metadata retained and recovery writes disabled")
                                 .arg(document.id);
        } else {
            document.snapshotPath = storedSnapshot;
        }
        document.originalExisted = object.value(QStringLiteral("originalExisted")).toBool();
        document.originalSize = qint64(object.value(QStringLiteral("originalSize")).toDouble(-1));
        document.originalMtimeMs = qint64(object.value(QStringLiteral("originalMtimeMs")).toDouble(-1));
        document.originalSha256 = QByteArray::fromHex(
            object.value(QStringLiteral("originalSha256")).toString().toLatin1());
        assessRecoveryState(document);
        if (document.recoveryState == RecoveryState::SnapshotUnreadable)
            m_writesBlocked = true;
        const QString diagnostic = stateDiagnostic(document);
        if (!diagnostic.isEmpty())
            m_diagnostics << diagnostic;
        m_documents.push_back(document);
    }
    return true;
}

void SessionManager::updateDocument(const DocumentCheckpoint &checkpoint, const QByteArray &content)
{
    if (m_writesBlocked)
        return;
    if (checkpoint.id.isEmpty()) {
        m_diagnostics << QStringLiteral("Refused checkpoint with empty document identity");
        return;
    }
    int index = indexOf(checkpoint.id);
    if (index < 0) {
        RecoveryDocument document;
        document.id = checkpoint.id;
        document.filePath = checkpoint.filePath;
        document.untitledNumber = checkpoint.untitledNumber;
        document.dirty = checkpoint.dirty;
        captureOriginalMetadata(document);
        m_documents.push_back(document);
        index = m_documents.size() - 1;
    }
    RecoveryDocument &document = m_documents[index];
    const bool becameCleanAfterSave = document.dirty && !checkpoint.dirty;
    if (document.filePath != checkpoint.filePath) {
        document.filePath = checkpoint.filePath;
        captureOriginalMetadata(document);
    } else if (becameCleanAfterSave) {
        captureOriginalMetadata(document);
    }
    document.untitledNumber = checkpoint.untitledNumber;
    document.dirty = checkpoint.dirty;
    document.recoveryState = RecoveryState::Ready;
    if (document.dirty) {
        document.snapshotPath = relativeSnapshotPath(document.id);
        m_obsoleteSnapshots.removeAll(absoluteSnapshotPath(document));
        m_pendingSnapshots.insert(document.id, content);
    } else {
        m_pendingSnapshots.remove(document.id);
        const QString oldSnapshot = absoluteSnapshotPath(document);
        document.snapshotPath.clear();
        if (!oldSnapshot.isEmpty() && QFileInfo::exists(oldSnapshot) &&
            !m_obsoleteSnapshots.contains(oldSnapshot))
            m_obsoleteSnapshots << oldSnapshot;
    }
    m_metadataDirty = true;
    scheduleCheckpoint();
}

void SessionManager::removeDocument(const QString &id)
{
    if (m_writesBlocked)
        return;
    const int index = indexOf(id);
    if (index < 0)
        return;
    const QString snapshot = absoluteSnapshotPath(m_documents[index]);
    m_documents.removeAt(index);
    m_pendingSnapshots.remove(id);
    if (!snapshot.isEmpty() && QFileInfo::exists(snapshot) &&
        !m_obsoleteSnapshots.contains(snapshot))
        m_obsoleteSnapshots << snapshot;
    if (m_activeDocumentId == id)
        m_activeDocumentId.clear();
    m_metadataDirty = true;
    scheduleCheckpoint();
}

void SessionManager::setSessionLayout(const QStringList &orderedIds, const QString &activeId)
{
    if (m_writesBlocked)
        return;
    QVector<RecoveryDocument> ordered;
    QSet<QString> added;
    ordered.reserve(m_documents.size());
    for (const QString &id : orderedIds) {
        const int index = indexOf(id);
        if (index >= 0 && !added.contains(id)) {
            ordered.push_back(m_documents[index]);
            added.insert(id);
        }
    }
    for (const RecoveryDocument &document : std::as_const(m_documents)) {
        if (!added.contains(document.id))
            ordered.push_back(document);
    }
    m_documents = ordered;
    m_activeDocumentId = activeId;
    m_metadataDirty = true;
    scheduleCheckpoint();
}

void SessionManager::captureOriginalMetadata(RecoveryDocument &document)
{
    document.originalExisted = !document.filePath.isEmpty() && QFileInfo::exists(document.filePath);
    document.originalSize = -1;
    document.originalMtimeMs = -1;
    document.originalSha256.clear();
    if (!document.originalExisted)
        return;
    const QFileInfo info(document.filePath);
    document.originalSize = info.size();
    document.originalMtimeMs = info.lastModified().toMSecsSinceEpoch();
    document.originalSha256 = fileHash(document.filePath);
}

void SessionManager::assessRecoveryState(RecoveryDocument &document)
{
    if (document.dirty && (document.snapshotPath.isEmpty() ||
                           !QFileInfo::exists(absoluteSnapshotPath(document)))) {
        document.recoveryState = RecoveryState::SnapshotMissing;
        return;
    }
    if (document.dirty) {
        QFile snapshot(absoluteSnapshotPath(document));
        if (!snapshot.open(QIODevice::ReadOnly)) {
            document.recoveryState = RecoveryState::SnapshotUnreadable;
            return;
        }
    }
    if (document.filePath.isEmpty()) {
        document.recoveryState = RecoveryState::Ready;
        return;
    }
    if (!QFileInfo::exists(document.filePath)) {
        document.recoveryState = RecoveryState::OriginalMissing;
        return;
    }
    const QFileInfo current(document.filePath);
    if (!document.originalExisted || current.size() != document.originalSize ||
        current.lastModified().toMSecsSinceEpoch() != document.originalMtimeMs ||
        fileHash(document.filePath) != document.originalSha256) {
        document.recoveryState = RecoveryState::OriginalChanged;
        return;
    }
    document.recoveryState = RecoveryState::Ready;
}

QString SessionManager::relativeSnapshotPath(const QString &id) const
{
    const QByteArray safeName = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("snapshots/%1.snapshot").arg(QString::fromLatin1(safeName));
}

QString SessionManager::absoluteSnapshotPath(const RecoveryDocument &document) const
{
    if (document.snapshotPath.isEmpty() ||
        document.snapshotPath != relativeSnapshotPath(document.id))
        return {};
    return QDir(m_storageDirectory).filePath(document.snapshotPath);
}

bool SessionManager::writeAtomic(const QString &path, const QByteArray &bytes, QString *error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void SessionManager::scheduleCheckpoint()
{
    m_checkpointTimer.start();
}

CheckpointStatus SessionManager::writeCheckpoint()
{
    if (m_writesBlocked) {
        const QString message = QStringLiteral("Recovery checkpoint blocked to preserve existing metadata");
        emit checkpointFailed(message);
        return CheckpointStatus::WritesBlocked;
    }
    if (!m_metadataDirty && m_pendingSnapshots.isEmpty())
        return CheckpointStatus::NoChanges;

    bool snapshotsOk = true;
    const auto pending = m_pendingSnapshots;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const int index = indexOf(it.key());
        if (index < 0)
            continue;
        QString error;
        const QString path = absoluteSnapshotPath(m_documents[index]);
        if (!writeAtomic(path, it.value(), &error)) {
            snapshotsOk = false;
            const QString message = QStringLiteral("Could not write recovery snapshot %1: %2").arg(path, error);
            m_diagnostics << message;
            emit checkpointFailed(message);
        } else {
            m_pendingSnapshots.remove(it.key());
        }
    }
    if (!snapshotsOk)
        return CheckpointStatus::SnapshotWriteFailed;

    QJsonArray documents;
    for (const RecoveryDocument &document : std::as_const(m_documents)) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), document.id);
        object.insert(QStringLiteral("filePath"), document.filePath);
        object.insert(QStringLiteral("untitledNumber"), document.untitledNumber);
        object.insert(QStringLiteral("dirty"), document.dirty);
        object.insert(QStringLiteral("snapshot"), document.snapshotPath);
        object.insert(QStringLiteral("originalExisted"), document.originalExisted);
        object.insert(QStringLiteral("originalSize"), double(document.originalSize));
        object.insert(QStringLiteral("originalMtimeMs"), double(document.originalMtimeMs));
        object.insert(QStringLiteral("originalSha256"), QString::fromLatin1(document.originalSha256.toHex()));
        documents.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    root.insert(QStringLiteral("activeDocumentId"), m_activeDocumentId);
    root.insert(QStringLiteral("documents"), documents);
    root.insert(QStringLiteral("checkpointIntervalMs"), m_checkpointTimer.interval());
    QString error;
    if (!writeAtomic(m_sessionFilePath, QJsonDocument(root).toJson(), &error)) {
        const QString message = QStringLiteral("Could not write session metadata: %1").arg(error);
        m_diagnostics << message;
        emit checkpointFailed(message);
        return CheckpointStatus::MetadataWriteFailed;
    }
    m_metadataDirty = false;
    QSet<QString> referencedSnapshots;
    for (const RecoveryDocument &document : std::as_const(m_documents)) {
        const QString path = absoluteSnapshotPath(document);
        if (!path.isEmpty())
            referencedSnapshots.insert(path);
    }
    const QStringList obsolete = m_obsoleteSnapshots;
    for (const QString &path : obsolete) {
        if (referencedSnapshots.contains(path)) {
            m_obsoleteSnapshots.removeAll(path);
            continue;
        }
        if (!QFileInfo::exists(path) || QFile::remove(path))
            m_obsoleteSnapshots.removeAll(path);
        else
            m_diagnostics << QStringLiteral("Could not remove obsolete recovery snapshot: %1")
                                 .arg(path);
    }
    return CheckpointStatus::Durable;
}

bool SessionManager::importLegacyDirectory(const QString &directory)
{
    const QString metadataPath = QDir(directory).filePath(QStringLiteral("session.json"));
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        m_diagnostics << QStringLiteral("Legacy metadata is malformed and was retained: %1").arg(metadataPath);
        return false;
    }
    const QJsonObject root = json.object();
    if (!root.value(QStringLiteral("untitledTabs")).isArray())
        return false;

    QSet<int> seen;
    for (const QJsonValue &value : root.value(QStringLiteral("untitledTabs")).toArray()) {
        const QJsonObject old = value.toObject();
        const int number = old.value(QStringLiteral("tabNumber")).toInt();
        const QString oldBackup = old.value(QStringLiteral("backupPath")).toString();
        if (number <= 0 || oldBackup.isEmpty() || seen.contains(number))
            continue;
        seen.insert(number);
        QFile backup(oldBackup);
        if (!backup.open(QIODevice::ReadOnly)) {
            m_diagnostics << QStringLiteral("Legacy recovery backup is missing; source metadata retained: %1")
                                 .arg(oldBackup);
            continue;
        }
        RecoveryDocument document;
        document.id = QStringLiteral("legacy-untitled-%1").arg(number);
        document.untitledNumber = number;
        document.dirty = true;
        document.snapshotPath = relativeSnapshotPath(document.id);
        m_documents.push_back(document);
        m_pendingSnapshots.insert(document.id, backup.readAll());
    }
    const int activeNumber = root.value(QStringLiteral("activeTab")).toInt();
    if (seen.contains(activeNumber))
        m_activeDocumentId = QStringLiteral("legacy-untitled-%1").arg(activeNumber);
    if (!m_documents.isEmpty()) {
        m_diagnostics << QStringLiteral("Imported legacy recovery data without modifying %1").arg(directory);
        return true;
    }
    return false;
}

int SessionManager::indexOf(const QString &id) const
{
    for (int i = 0; i < m_documents.size(); ++i) {
        if (m_documents[i].id == id)
            return i;
    }
    return -1;
}

CheckpointStatus SessionManager::flush()
{
    m_checkpointTimer.stop();
    return writeCheckpoint();
}

CheckpointStatus SessionManager::shutdown()
{
    return flush();
}

QVector<RecoveryDocument> SessionManager::documents() const
{
    return m_documents;
}

QString SessionManager::activeDocumentId() const
{
    return m_activeDocumentId;
}

RecoveryReadResult SessionManager::readRecoveryContent(const RecoveryDocument &document) const
{
    if (document.dirty && m_pendingSnapshots.contains(document.id))
        return {true, m_pendingSnapshots.value(document.id), {}};
    const QString path = document.dirty ? absoluteSnapshotPath(document) : document.filePath;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {false, {}, file.errorString()};
    return {true, file.readAll(), {}};
}

QStringList SessionManager::diagnostics() const
{
    return m_diagnostics;
}

QString SessionManager::storageDirectory() const
{
    return m_storageDirectory;
}

QString SessionManager::sessionFilePath() const
{
    return m_sessionFilePath;
}

bool SessionManager::writesBlocked() const
{
    return m_writesBlocked;
}
