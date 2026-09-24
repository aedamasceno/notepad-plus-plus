#include "sessionmanager.h"
#include "languagecatalog.h"

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
#include <algorithm>
#include <cmath>
#include <limits>
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

bool jsonInteger(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 *result)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < double(minimum) || number > double(maximum))
        return false;
    if (result)
        *result = qint64(number);
    return true;
}

bool lowerHex(const QString &value, qsizetype length)
{
    if (value.size() != length)
        return false;
    for (const QChar character : value) {
        if ((character < QLatin1Char('0') || character > QLatin1Char('9')) &&
            (character < QLatin1Char('a') || character > QLatin1Char('f')))
            return false;
    }
    return true;
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
        QDir(generic).filePath(QStringLiteral("npp_linux/notepad++/sessions")),
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
            if (m_writesBlocked)
                break;
        }
        return;
    }

    QFile file(m_sessionFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Could not read session metadata: %1").arg(file.errorString());
        return;
    }
    QJsonParseError parseError;
    const QByteArray metadataBytes = file.readAll();
    const QJsonDocument json = QJsonDocument::fromJson(metadataBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Malformed session metadata retained at %1; recovery writes are disabled: %2")
                             .arg(m_sessionFilePath, parseError.errorString());
        return;
    }
    const QJsonObject root = json.object();
    if (!root.contains(QStringLiteral("schemaVersion")) &&
        root.value(QStringLiteral("untitledTabs")).isArray()) {
        const QString preservedPath =
            QDir(m_storageDirectory).filePath(QStringLiteral("session.legacy.json"));
        bool preserved = false;
        QFile existing(preservedPath);
        if (existing.exists() && existing.open(QIODevice::ReadOnly))
            preserved = existing.readAll() == metadataBytes;
        if (!preserved && !QFileInfo::exists(preservedPath)) {
            QString error;
            preserved = writeAtomic(preservedPath, metadataBytes, &error);
            if (!preserved)
                m_diagnostics << QStringLiteral("Could not preserve canonical legacy metadata: %1")
                                     .arg(error);
        }
        if (!preserved) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Canonical legacy metadata was retained; migration writes are disabled");
            return;
        }
        if (importLegacyDirectory(m_storageDirectory)) {
            m_metadataDirty = true;
        } else {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Canonical legacy metadata had no readable backups and was retained");
        }
        return;
    }
    qint64 parsedSchemaVersion = 0;
    if (!jsonInteger(root.value(QStringLiteral("schemaVersion")), 2, SchemaVersion,
                     &parsedSchemaVersion) ||
        !root.value(QStringLiteral("activeDocumentId")).isString()) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Session schema metadata has invalid required types; recovery writes are disabled");
        return;
    }
    const int schemaVersion = int(parsedSchemaVersion);
    if (schemaVersion != 2 && schemaVersion != 3 && schemaVersion != 4 &&
        schemaVersion != SchemaVersion) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Unsupported session schema retained at %1; recovery writes are disabled")
                             .arg(m_sessionFilePath);
        return;
    }
    if (schemaVersion == 2) {
        if (loadVersion2(root)) {
            for (const RecoveryDocument &document : std::as_const(m_documents))
                m_primaryDocumentIds << document.id;
            m_activePane = QStringLiteral("primary");
            m_splitterOrientation = Qt::Horizontal;
            m_splitterSizes.clear();
            if (canonicalizeLegacySnapshots(true))
                m_metadataDirty = true;
        }
    } else if (schemaVersion == 3) {
        if (loadVersion3(root) && canonicalizeLegacySnapshots())
            m_metadataDirty = true;
    }
    else if (schemaVersion == 4)
        loadVersion4(root);
    else
        loadVersion5(root);
}

bool SessionManager::loadVersion5(const QJsonObject &root)
{
    if (!loadVersion4(root))
        return false;
    QHash<QString, QJsonObject> storedById;
    for (const QJsonValue &value : root.value(QStringLiteral("documents")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        storedById.insert(object.value(QStringLiteral("id")).toString(), object);
    }
    for (RecoveryDocument &document : m_documents) {
        const QJsonObject object = storedById.value(document.id);
        const QJsonValue language = object.value(QStringLiteral("languageId"));
        const QJsonValue automatic = object.value(QStringLiteral("languageAutomatic"));
        if (language.isString() && LanguageCatalog::find(language.toString()) && automatic.isBool()) {
            document.languageId = language.toString();
            document.languageAutomatic = automatic.toBool();
        } else {
            m_diagnostics << QStringLiteral("Optional language state was malformed for %1; automatic detection will be used")
                                 .arg(document.id);
        }
    }

    const QJsonValue viewsValue = root.value(QStringLiteral("views"));
    if (!viewsValue.isArray()) {
        m_diagnostics << QStringLiteral("Optional editor view state was malformed and was ignored");
        return true;
    }
    QSet<QString> known;
    for (const RecoveryDocument &document : std::as_const(m_documents)) known.insert(document.id);
    QSet<QString> seen;
    QVector<RecoveryViewState> parsed;
    bool valid = true;
    for (const QJsonValue &value : viewsValue.toArray()) {
        if (!value.isObject()) { valid = false; break; }
        const QJsonObject object = value.toObject();
        const QJsonValue id = object.value(QStringLiteral("documentId"));
        const QJsonValue pane = object.value(QStringLiteral("pane"));
        qint64 position = 0, anchor = 0, first = 0, x = 0;
        if (!id.isString() || !known.contains(id.toString()) || !pane.isString() ||
            (pane.toString() != QStringLiteral("primary") && pane.toString() != QStringLiteral("secondary")) ||
            !jsonInteger(object.value(QStringLiteral("position")), 0, qint64(1) << 53, &position) ||
            !jsonInteger(object.value(QStringLiteral("anchor")), 0, qint64(1) << 53, &anchor) ||
            !jsonInteger(object.value(QStringLiteral("firstVisibleLine")), 0, std::numeric_limits<int>::max(), &first) ||
            !jsonInteger(object.value(QStringLiteral("xOffset")), 0, std::numeric_limits<int>::max(), &x)) {
            valid = false; break;
        }
        const QString key = id.toString() + QLatin1Char('\n') + pane.toString();
        if (seen.contains(key)) { valid = false; break; }
        seen.insert(key);
        parsed.push_back({id.toString(), pane.toString(), position, anchor, int(first), int(x)});
    }
    if (valid)
        m_viewStates = parsed;
    else
        m_diagnostics << QStringLiteral("Optional editor view state was malformed and was ignored");
    return true;
}

bool SessionManager::loadVersion4(const QJsonObject &root)
{
    if (!loadVersion3(root))
        return false;
    QHash<QString, QJsonObject> storedById;
    for (const QJsonValue &value : root.value(QStringLiteral("documents")).toArray()) {
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(QStringLiteral("id")).toString();
            if (!storedById.contains(id))
                storedById.insert(id, object);
        }
    }
    for (RecoveryDocument &document : m_documents) {
        const QJsonObject object = storedById.value(document.id);
        DocumentFormat::TextEncoding encoding;
        DocumentFormat::EolKind eol;
        DocumentFormat::EolKind insertion;
        if (object.isEmpty() ||
            !DocumentFormat::encodingFromKey(object.value(QStringLiteral("encoding")).toString(), &encoding) ||
            !DocumentFormat::eolFromKey(object.value(QStringLiteral("eol")).toString(), &eol) ||
            !DocumentFormat::eolFromKey(object.value(QStringLiteral("insertionEol")).toString(), &insertion) ||
            insertion == DocumentFormat::EolKind::None || insertion == DocumentFormat::EolKind::Mixed) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Encoding/EOL session metadata is malformed; metadata retained and recovery writes disabled");
            return false;
        }
        document.encoding = encoding;
        document.eol = eol;
        document.insertionEol = insertion;
        document.legacyEncodedSnapshot = false;
        if (document.dirty && document.recoveryState != RecoveryState::SnapshotMissing &&
            document.recoveryState != RecoveryState::SnapshotUnreadable) {
            if (!contentAddressedSnapshotHash(document.id, document.snapshotPath)) {
                document.recoveryState = RecoveryState::SnapshotUnreadable;
                m_writesBlocked = true;
                m_diagnostics << QStringLiteral("Canonical recovery snapshot path is malformed; metadata retained and recovery writes disabled");
                continue;
            }
            const RecoveryReadResult recovered = readRecoveryContent(document);
            if (!recovered.success) {
                document.recoveryState = RecoveryState::SnapshotUnreadable;
                m_writesBlocked = true;
                m_diagnostics << stateDiagnostic(document);
            }
        }
    }
    return !m_writesBlocked;
}

bool SessionManager::loadVersion3(const QJsonObject &root)
{
    if (!loadVersion2(root)) return false;
    const QJsonValue dualValue = root.value(QStringLiteral("dualView"));
    if (!dualValue.isObject()) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view session metadata is malformed; metadata retained and recovery writes disabled");
        return false;
    }
    const QJsonObject dual = dualValue.toObject();
    auto readIds = [this](const QJsonValue &value, QStringList &out) {
        if (!value.isArray()) return false;
        QSet<QString> valid; for (const auto &document : m_documents) valid.insert(document.id);
        QSet<QString> seen;
        for (const QJsonValue &entry : value.toArray()) {
            if (!entry.isString() || !valid.contains(entry.toString()) || seen.contains(entry.toString()))
                return false;
            seen.insert(entry.toString());
            out << entry.toString();
        }
        return true;
    };
    if (!readIds(dual.value(QStringLiteral("primary")), m_primaryDocumentIds) ||
        !readIds(dual.value(QStringLiteral("secondary")), m_secondaryDocumentIds)) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view pane assignments are malformed; metadata retained and recovery writes disabled");
        return false;
    }
    QSet<QString> recoveredIds;
    for (const RecoveryDocument &document : std::as_const(m_documents))
        recoveredIds.insert(document.id);
    QSet<QString> assignedIds(m_primaryDocumentIds.cbegin(), m_primaryDocumentIds.cend());
    assignedIds.unite(QSet<QString>(m_secondaryDocumentIds.cbegin(), m_secondaryDocumentIds.cend()));
    if (assignedIds != recoveredIds) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view pane assignments are incomplete; metadata retained and recovery writes disabled");
        return false;
    }
    m_activePane = dual.value(QStringLiteral("activePane")).toString();
    if (m_activePane != QStringLiteral("primary") && m_activePane != QStringLiteral("secondary")) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view active pane is malformed; metadata retained and recovery writes disabled");
        return false;
    }
    const QString orientation = dual.value(QStringLiteral("orientation")).toString();
    if (orientation != QStringLiteral("horizontal") && orientation != QStringLiteral("vertical")) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view orientation is malformed; metadata retained and recovery writes disabled");
        return false;
    }
    m_splitterOrientation = orientation == QStringLiteral("vertical") ? Qt::Vertical : Qt::Horizontal;
    const QJsonValue sizesValue = dual.value(QStringLiteral("sizes"));
    if (!sizesValue.isArray() ||
        (sizesValue.toArray().size() != 0 && sizesValue.toArray().size() != 2)) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view splitter sizes are malformed; metadata retained and recovery writes disabled");
        return false;
    }
    for (const QJsonValue &size : sizesValue.toArray()) {
        if (!size.isDouble() || size.toInt() < 0) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Dual-view splitter sizes are malformed; metadata retained and recovery writes disabled");
            return false;
        }
        m_splitterSizes << size.toInt();
    }
    const QStringList &activeIds = m_activePane == QStringLiteral("primary")
                                       ? m_primaryDocumentIds : m_secondaryDocumentIds;
    if (!m_activeDocumentId.isEmpty() && !activeIds.contains(m_activeDocumentId)) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Dual-view active document assignment is malformed; metadata retained and recovery writes disabled");
        return false;
    }
    // Schema 3 had no format fields. Detect its preserved bytes without writing
    // anything; a later successful checkpoint performs the schema-4 migration.
    for (RecoveryDocument &document : m_documents) {
        document.legacyEncodedSnapshot = document.dirty;
        const RecoveryReadResult content = readRecoveryContent(document);
        if (!content.success)
            continue;
        const auto decoded = DocumentFormat::decode(content.content);
        if (decoded.success) {
            document.encoding = decoded.encoding;
            document.eol = decoded.eol.kind;
            document.insertionEol = decoded.eol.insertion;
            document.legacyEncodedSnapshot = document.dirty;
        }
    }
    return true;
}

bool SessionManager::canonicalizeLegacySnapshots(bool allowMissingSnapshots)
{
    if (m_writesBlocked)
        return false;

    struct MigrationPlan {
        int documentIndex = -1;
        QString oldSnapshot;
        QString newSnapshot;
        DocumentFormat::DecodedDocument decoded;
    };
    QVector<MigrationPlan> plans;
    bool allCanonicalized = true;
    for (RecoveryDocument &document : m_documents) {
        if (document.dirty)
            document.legacyEncodedSnapshot = true;
    }
    for (int index = 0; index < m_documents.size(); ++index) {
        RecoveryDocument &document = m_documents[index];
        if (!document.dirty)
            continue;
        if (allowMissingSnapshots &&
            document.recoveryState == RecoveryState::SnapshotMissing) {
            allCanonicalized = false;
            continue;
        }
        const RecoveryReadResult content = readRecoveryContent(document);
        if (!content.success) {
            document.recoveryState = RecoveryState::SnapshotUnreadable;
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Legacy recovery snapshot could not be read; source retained and migration blocked: %1")
                                 .arg(document.id);
            return false;
        }
        const DocumentFormat::DecodedDocument decoded = DocumentFormat::decode(content.content);
        if (!decoded.success) {
            document.recoveryState = RecoveryState::SnapshotUnreadable;
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Legacy recovery snapshot is not valid text; source retained and migration blocked: %1")
                                 .arg(document.id);
            return false;
        }
        plans.push_back({index, absoluteSnapshotPath(document),
                         relativeSnapshotPath(document.id, decoded.utf8), decoded});
    }

    for (const MigrationPlan &plan : std::as_const(plans)) {
        RecoveryDocument &document = m_documents[plan.documentIndex];
        document.encoding = plan.decoded.encoding;
        document.eol = plan.decoded.eol.kind;
        document.insertionEol = plan.decoded.eol.insertion;
        document.snapshotPath = plan.newSnapshot;
        document.legacyEncodedSnapshot = false;
        m_pendingSnapshots.insert(document.id, plan.decoded.utf8);
        if (!plan.oldSnapshot.isEmpty() && plan.oldSnapshot != absoluteSnapshotPath(document) &&
            QFileInfo::exists(plan.oldSnapshot) && !m_obsoleteSnapshots.contains(plan.oldSnapshot))
            m_obsoleteSnapshots << plan.oldSnapshot;
    }
    return allCanonicalized;
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
    QSet<int> untitledNumbers;
    for (const QJsonValue &value : documentsValue.toArray()) {
        if (!value.isObject()) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Ignored malformed document entry; metadata retained and recovery writes disabled");
            continue;
        }
        const QJsonObject object = value.toObject();
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        const QJsonValue filePathValue = object.value(QStringLiteral("filePath"));
        const QJsonValue untitledValue = object.value(QStringLiteral("untitledNumber"));
        const QJsonValue dirtyValue = object.value(QStringLiteral("dirty"));
        const QJsonValue snapshotValue = object.value(QStringLiteral("snapshot"));
        const QJsonValue originalExistedValue = object.value(QStringLiteral("originalExisted"));
        const QJsonValue originalSizeValue = object.value(QStringLiteral("originalSize"));
        const QJsonValue originalMtimeValue = object.value(QStringLiteral("originalMtimeMs"));
        const QJsonValue originalHashValue = object.value(QStringLiteral("originalSha256"));
        qint64 untitledNumber = 0;
        qint64 originalSize = -1;
        qint64 originalMtime = -1;
        if (!idValue.isString() || idValue.toString().isEmpty() ||
            !filePathValue.isString() ||
            !jsonInteger(untitledValue, 0, std::numeric_limits<int>::max(), &untitledNumber) ||
            !dirtyValue.isBool() || !snapshotValue.isString() ||
            !originalExistedValue.isBool() ||
            !jsonInteger(originalSizeValue, -1, qint64(1) << 53, &originalSize) ||
            !jsonInteger(originalMtimeValue, -1, qint64(1) << 53, &originalMtime) ||
            !originalHashValue.isString()) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Rejected recovery document with invalid required field types; metadata retained and recovery writes disabled");
            continue;
        }
        RecoveryDocument document;
        document.id = idValue.toString();
        if (document.id.isEmpty() || seen.contains(document.id)) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Ignored empty or duplicate recovery identity; metadata retained and recovery writes disabled");
            continue;
        }
        seen.insert(document.id);
        document.filePath = filePathValue.toString();
        document.untitledNumber = int(untitledNumber);
        if (document.filePath.isEmpty()) {
            if (document.untitledNumber <= 0 || untitledNumbers.contains(document.untitledNumber)) {
                m_writesBlocked = true;
                m_diagnostics << QStringLiteral("Rejected non-positive or duplicate untitled document number; metadata retained and recovery writes disabled");
                continue;
            } else {
                untitledNumbers.insert(document.untitledNumber);
            }
        }
        document.dirty = dirtyValue.toBool();
        const QString storedSnapshot = snapshotValue.toString();
        if ((document.dirty && storedSnapshot.isEmpty()) ||
            (!document.dirty && !storedSnapshot.isEmpty()) ||
            (!storedSnapshot.isEmpty() && !isManagedSnapshotPath(document.id, storedSnapshot))) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Rejected inconsistent or unmanaged snapshot path for %1; metadata retained and recovery writes disabled")
                                 .arg(document.id);
            continue;
        } else {
            document.snapshotPath = storedSnapshot;
        }
        document.originalExisted = originalExistedValue.toBool();
        document.originalSize = originalSize;
        document.originalMtimeMs = originalMtime;
        const QString originalHash = originalHashValue.toString();
        const bool originalMetadataValid = document.originalExisted
            ? document.originalSize >= 0 && document.originalMtimeMs >= 0 && lowerHex(originalHash, 64)
            : document.originalSize == -1 && document.originalMtimeMs == -1 && originalHash.isEmpty();
        if (!originalMetadataValid) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Rejected inconsistent original-file metadata; metadata retained and recovery writes disabled");
            continue;
        } else {
            document.originalSha256 = QByteArray::fromHex(originalHash.toLatin1());
        }
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
        if (!m_primaryDocumentIds.contains(document.id) &&
            !m_secondaryDocumentIds.contains(document.id))
            m_primaryDocumentIds << document.id;
        index = m_documents.size() - 1;
    }
    RecoveryDocument &document = m_documents[index];
    const bool becameCleanAfterSave = document.dirty && !checkpoint.dirty;
    if (document.filePath != checkpoint.filePath) {
        document.filePath = checkpoint.filePath;
        captureOriginalMetadata(document);
    } else if (becameCleanAfterSave || !checkpoint.dirty) {
        captureOriginalMetadata(document);
    }
    document.untitledNumber = checkpoint.untitledNumber;
    document.dirty = checkpoint.dirty;
    document.encoding = checkpoint.encoding;
    document.eol = checkpoint.eol;
    document.insertionEol = checkpoint.insertionEol;
    document.languageId = checkpoint.languageId.isEmpty() ? QStringLiteral("plain")
                                                           : checkpoint.languageId;
    document.languageAutomatic = checkpoint.languageAutomatic;
    document.legacyEncodedSnapshot = false;
    document.recoveryState = RecoveryState::Ready;
    if (document.dirty) {
        const QString oldSnapshot = absoluteSnapshotPath(document);
        document.snapshotPath = relativeSnapshotPath(document.id, content);
        if (!oldSnapshot.isEmpty() && oldSnapshot != absoluteSnapshotPath(document) &&
            QFileInfo::exists(oldSnapshot) && !m_obsoleteSnapshots.contains(oldSnapshot))
            m_obsoleteSnapshots << oldSnapshot;
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
    const bool removedFromPrimary = m_primaryDocumentIds.removeAll(id) > 0;
    const bool removedFromSecondary = m_secondaryDocumentIds.removeAll(id) > 0;
    const bool layoutChanged = removedFromPrimary || removedFromSecondary;
    const int index = indexOf(id);
    if (index < 0) {
        if (layoutChanged) {
            m_metadataDirty = true;
            scheduleCheckpoint();
        }
        return;
    }
    const QString snapshot = absoluteSnapshotPath(m_documents[index]);
    m_documents.removeAt(index);
    m_pendingSnapshots.remove(id);
    m_viewStates.erase(std::remove_if(m_viewStates.begin(), m_viewStates.end(),
        [&id](const RecoveryViewState &state) { return state.documentId == id; }),
        m_viewStates.end());
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

void SessionManager::setDualViewLayout(const QStringList &primaryIds,
                                       const QStringList &secondaryIds,
                                       const QString &activePane,
                                       Qt::Orientation orientation,
                                       const QList<int> &splitterSizes)
{
    if (m_writesBlocked) return;
    m_primaryDocumentIds = primaryIds;
    m_secondaryDocumentIds = secondaryIds;
    m_activePane = activePane == QStringLiteral("secondary") ? activePane : QStringLiteral("primary");
    m_splitterOrientation = orientation;
    m_splitterSizes = splitterSizes;
    m_metadataDirty = true;
    scheduleCheckpoint();
}

void SessionManager::setViewStates(const QVector<RecoveryViewState> &states)
{
    if (m_writesBlocked)
        return;
    m_viewStates = states;
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

QString SessionManager::relativeSnapshotPath(const QString &id, const QByteArray &content) const
{
    const QString legacy = relativeSnapshotPath(id);
    const QString idHash = legacy.mid(QStringLiteral("snapshots/").size(), 64);
    const QString contentHash = QString::fromLatin1(
        QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    return QStringLiteral("snapshots/%1-%2.snapshot").arg(idHash, contentHash);
}

bool SessionManager::isManagedSnapshotPath(const QString &id, const QString &path) const
{
    if (path == relativeSnapshotPath(id))
        return true;
    return contentAddressedSnapshotHash(id, path);
}

bool SessionManager::contentAddressedSnapshotHash(const QString &id, const QString &path,
                                                   QByteArray *hash) const
{
    const QString idHash = QString::fromLatin1(
        QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString prefix = QStringLiteral("snapshots/%1-").arg(idHash);
    const QString suffix = QStringLiteral(".snapshot");
    if (!path.startsWith(prefix) || !path.endsWith(suffix))
        return false;
    const QString contentHash = path.mid(prefix.size(), path.size() - prefix.size() - suffix.size());
    if (contentHash.size() != 64)
        return false;
    for (const QChar character : contentHash) {
        if ((character < QLatin1Char('0') || character > QLatin1Char('9')) &&
            (character < QLatin1Char('a') || character > QLatin1Char('f')))
            return false;
    }
    if (hash)
        *hash = QByteArray::fromHex(contentHash.toLatin1());
    return true;
}

QString SessionManager::absoluteSnapshotPath(const RecoveryDocument &document) const
{
    if (document.snapshotPath.isEmpty() ||
        !isManagedSnapshotPath(document.id, document.snapshotPath))
        return {};
    return QDir(m_storageDirectory).filePath(document.snapshotPath);
}

bool SessionManager::writeAtomic(const QString &path, const QByteArray &bytes, QString *error) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
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
    for (const RecoveryDocument &document : std::as_const(m_documents)) {
        if (document.dirty && document.legacyEncodedSnapshot) {
            if (m_pendingSnapshots.isEmpty())
                return CheckpointStatus::NoChanges;
            const QString message = QStringLiteral("Recovery checkpoint blocked until the legacy snapshot can be replaced safely");
            m_diagnostics << message;
            emit checkpointFailed(message);
            return CheckpointStatus::WritesBlocked;
        }
    }

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
        object.insert(QStringLiteral("encoding"), DocumentFormat::encodingKey(document.encoding));
        object.insert(QStringLiteral("eol"), DocumentFormat::eolKey(document.eol));
        object.insert(QStringLiteral("insertionEol"), DocumentFormat::eolKey(document.insertionEol));
        object.insert(QStringLiteral("languageId"), document.languageId);
        object.insert(QStringLiteral("languageAutomatic"), document.languageAutomatic);
        documents.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    root.insert(QStringLiteral("activeDocumentId"), m_activeDocumentId);
    root.insert(QStringLiteral("documents"), documents);
    QJsonArray primary; for (const QString &id : m_primaryDocumentIds) primary.append(id);
    QJsonArray secondary; for (const QString &id : m_secondaryDocumentIds) secondary.append(id);
    QJsonArray sizes; for (int size : m_splitterSizes) sizes.append(size);
    root.insert(QStringLiteral("dualView"), QJsonObject{
        {QStringLiteral("primary"), primary}, {QStringLiteral("secondary"), secondary},
        {QStringLiteral("activePane"), m_activePane},
        {QStringLiteral("orientation"), m_splitterOrientation == Qt::Vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal")},
        {QStringLiteral("sizes"), sizes}});
    QJsonArray views;
    for (const RecoveryViewState &state : std::as_const(m_viewStates)) {
        views.append(QJsonObject{{QStringLiteral("documentId"), state.documentId},
                                 {QStringLiteral("pane"), state.pane},
                                 {QStringLiteral("position"), double(state.position)},
                                 {QStringLiteral("anchor"), double(state.anchor)},
                                 {QStringLiteral("firstVisibleLine"), state.firstVisibleLine},
                                 {QStringLiteral("xOffset"), state.xOffset}});
    }
    root.insert(QStringLiteral("views"), views);
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

    for (const QJsonValue &value : root.value(QStringLiteral("untitledTabs")).toArray()) {
        if (!value.isObject()) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Legacy recovery manifest is malformed; source retained and migration blocked");
            return false;
        }
        const QJsonObject old = value.toObject();
        qint64 number = 0;
        if (!jsonInteger(old.value(QStringLiteral("tabNumber")), 1,
                         std::numeric_limits<int>::max(), &number) ||
            !old.value(QStringLiteral("backupPath")).isString() ||
            old.value(QStringLiteral("backupPath")).toString().isEmpty()) {
            m_writesBlocked = true;
            m_diagnostics << QStringLiteral("Legacy recovery manifest is malformed; source retained and migration blocked");
            return false;
        }
    }

    QVector<int> identityOrder;
    QHash<int, QStringList> backupCandidates;
    for (const QJsonValue &value : root.value(QStringLiteral("untitledTabs")).toArray()) {
        const QJsonObject old = value.toObject();
        const int number = old.value(QStringLiteral("tabNumber")).toInt();
        const QString oldBackup = old.value(QStringLiteral("backupPath")).toString();
        if (!backupCandidates.contains(number))
            identityOrder.push_back(number);
        backupCandidates[number].push_back(oldBackup);
    }

    QVector<RecoveryDocument> importedDocuments;
    QHash<QString, QByteArray> importedSnapshots;
    bool incomplete = false;
    for (int number : std::as_const(identityOrder)) {
        bool imported = false;
        QStringList failures;
        for (const QString &oldBackup : std::as_const(backupCandidates[number])) {
            QFile backup(oldBackup);
            if (!backup.open(QIODevice::ReadOnly)) {
                failures << QStringLiteral("Legacy recovery backup is unavailable; source retained and migration blocked: %1 (%2)")
                                .arg(oldBackup, backup.errorString());
                continue;
            }
            const QByteArray sourceBytes = backup.readAll();
            if (backup.error() != QFileDevice::NoError) {
                failures << QStringLiteral("Legacy recovery backup could not be read; source retained and migration blocked: %1 (%2)")
                                .arg(oldBackup, backup.errorString());
                continue;
            }
            const DocumentFormat::DecodedDocument decoded = DocumentFormat::decode(sourceBytes);
            if (!decoded.success) {
                failures << QStringLiteral("Legacy recovery backup is not valid text; source retained and migration blocked: %1")
                                .arg(oldBackup);
                continue;
            }
            RecoveryDocument document;
            document.id = QStringLiteral("legacy-untitled-%1").arg(number);
            document.untitledNumber = number;
            document.dirty = true;
            document.encoding = decoded.encoding;
            document.eol = decoded.eol.kind;
            document.insertionEol = decoded.eol.insertion;
            document.snapshotPath = relativeSnapshotPath(document.id, decoded.utf8);
            importedDocuments.push_back(document);
            importedSnapshots.insert(document.id, decoded.utf8);
            imported = true;
            break;
        }
        if (!imported) {
            incomplete = true;
            m_diagnostics.append(failures);
        }
    }
    if (incomplete) {
        m_writesBlocked = true;
        m_diagnostics << QStringLiteral("Legacy recovery migration requires every unique backup; no canonical recovery data was written");
        return false;
    }
    if (importedDocuments.isEmpty())
        return false;

    m_documents = importedDocuments;
    m_pendingSnapshots = importedSnapshots;
    m_primaryDocumentIds.clear();
    for (const RecoveryDocument &document : std::as_const(m_documents))
        m_primaryDocumentIds << document.id;
    m_secondaryDocumentIds.clear();
    m_activePane = QStringLiteral("primary");
    m_splitterOrientation = Qt::Horizontal;
    m_splitterSizes.clear();
    const int activeNumber = root.value(QStringLiteral("activeTab")).toInt();
    if (backupCandidates.contains(activeNumber))
        m_activeDocumentId = QStringLiteral("legacy-untitled-%1").arg(activeNumber);
    m_diagnostics << QStringLiteral("Imported legacy recovery data without modifying %1").arg(directory);
    return true;
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
    QByteArray bytes;
    if (document.dirty && m_pendingSnapshots.contains(document.id)) {
        bytes = m_pendingSnapshots.value(document.id);
    } else {
        const QString path = document.dirty ? absoluteSnapshotPath(document) : document.filePath;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {false, {}, file.errorString()};
        bytes = file.readAll();
    }
    if (document.dirty) {
        QByteArray expectedHash;
        const bool contentAddressed = contentAddressedSnapshotHash(
            document.id, document.snapshotPath, &expectedHash);
        if (contentAddressed &&
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != expectedHash)
            return {false, {}, QStringLiteral("Recovery snapshot content hash does not match its filename")};
        if (document.legacyEncodedSnapshot)
            return {true, bytes, {}};
        if (!contentAddressed)
            return {false, {}, QStringLiteral("Recovery snapshot path is not content-addressed")};
        if (!DocumentFormat::isValidUtf8Text(bytes))
            return {false, {}, QStringLiteral("Recovery snapshot is not canonical UTF-8 text")};
    }
    return {true, bytes, {}};
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
