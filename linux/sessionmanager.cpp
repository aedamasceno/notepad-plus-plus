#include "sessionmanager.h"
#include <QJsonDocument>
#include <QFile>
#include <QTextStream>
#include <QSaveFile>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
    , m_backupTimer(new QTimer(this))
    , m_shouldSaveSession(false)
    , m_activeTabNumber(0)
{
    // Set up backup timer
    m_backupTimer->setSingleShot(true);
    m_backupTimer->setInterval(5000); // 5 seconds delay
    
    connect(m_backupTimer, &QTimer::timeout, this, &SessionManager::saveSession);
    
    // Initialize session directory
    m_sessionDir = getSessionDir();
    if (!m_sessionDir.isEmpty()) {
        QDir dir(m_sessionDir);
        if (!dir.exists()) {
            dir.mkpath(m_sessionDir);
        }
        m_sessionFilePath = m_sessionDir + "/session.json";
    }
}

SessionManager::~SessionManager()
{
    // Ensure final save on destruction
    shutdown();
}

void SessionManager::saveSession()
{
    if (m_shouldSaveSession) {
        saveSessionFile();
        m_shouldSaveSession = false;
    }
}

void SessionManager::loadSession()
{
    loadSessionFile();
}

void SessionManager::clearSession()
{
    m_untitledTabs = QJsonArray();
    m_activeTabNumber = 0;
    m_backupFiles.clear();
    
    // Remove all backup files
    QDir dir(m_sessionDir);
    if (dir.exists()) {
        QStringList filters;
        filters << "new_*.backup";
        QStringList backupFiles = dir.entryList(filters, QDir::Files);
        for (const QString& file : backupFiles) {
            dir.remove(file);
        }
    }
}

void SessionManager::addUntitledDocument(const QString& content, int tabNumber)
{
    // Check if this tab number already exists
    bool exists = false;
    for (int i = 0; i < m_untitledTabs.size(); ++i) {
        QJsonObject tabObj = m_untitledTabs[i].toObject();
        if (tabObj["tabNumber"].toInt() == tabNumber) {
            exists = true;
            // Update existing entry
            QString backupPath = tabObj["backupPath"].toString();
            m_backupFiles[tabNumber] = backupPath;
            
            // Update the content in backup file
            QSaveFile saveFile(backupPath);
            if (saveFile.open(QIODevice::WriteOnly)) {
                QTextStream stream(&saveFile);
                stream << content;
                saveFile.commit();
            }
            
            // Update JSON array entry
            tabObj["backupPath"] = backupPath;
            m_untitledTabs[i] = tabObj;
            break;
        }
    }
    
    if (!exists) {
        // Create backup file
        QString backupPath = getBackupFilePath(tabNumber);
        
        // Save content to backup file
        QSaveFile saveFile(backupPath);
        if (saveFile.open(QIODevice::WriteOnly)) {
            QTextStream stream(&saveFile);
            stream << content;
            saveFile.commit();
            
            m_backupFiles[tabNumber] = backupPath;
            
            // Add to session data
            QJsonObject tabData;
            tabData["tabNumber"] = tabNumber;
            tabData["backupPath"] = backupPath;
            m_untitledTabs.append(tabData);
            
            m_shouldSaveSession = true;
            m_backupTimer->start();
        }
    }
}

void SessionManager::removeUntitledDocument(int tabNumber)
{
    // Remove from backup files map
    if (m_backupFiles.contains(tabNumber)) {
        QString backupPath = m_backupFiles[tabNumber];
        QFile::remove(backupPath);
        m_backupFiles.remove(tabNumber);
        
        // Remove from session data
        for (int i = 0; i < m_untitledTabs.size(); ++i) {
            QJsonObject tabObj = m_untitledTabs[i].toObject();
            if (tabObj["tabNumber"].toInt() == tabNumber) {
                m_untitledTabs.removeAt(i);
                break;
            }
        }
        
        m_shouldSaveSession = true;
        m_backupTimer->start();
    }
}

void SessionManager::updateUntitledDocumentContent(int tabNumber, const QString& content)
{
    if (m_backupFiles.contains(tabNumber)) {
        QString backupPath = m_backupFiles[tabNumber];
        
        // Save content to backup file
        QSaveFile saveFile(backupPath);
        if (saveFile.open(QIODevice::WriteOnly)) {
            QTextStream stream(&saveFile);
            stream << content;
            saveFile.commit();
            
            m_shouldSaveSession = true;
            m_backupTimer->start();
        }
    }
}

bool SessionManager::hasUntitledDocuments() const
{
    return !m_untitledTabs.isEmpty();
}

void SessionManager::shutdown()
{
    // Force immediate save on shutdown
    if (m_shouldSaveSession) {
        saveSessionFile();
    }
    
    // Cancel any pending timer
    m_backupTimer->stop();
}

QString SessionManager::getSessionDir() const
{
    QStringList paths = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    if (!paths.isEmpty()) {
        return paths.first() + "/notepad++/sessions";
    }
    return QString();
}

QString SessionManager::getBackupFilePath(int tabNumber) const
{
    if (m_sessionDir.isEmpty()) {
        return QString();
    }
    return m_sessionDir + "/new_" + QString::number(tabNumber) + ".backup";
}

QString SessionManager::getSessionFilePath() const
{
    return m_sessionFilePath;
}

void SessionManager::saveSessionFile()
{
    if (m_sessionDir.isEmpty()) {
        return;
    }
    
    QJsonObject sessionObject;
    sessionObject["activeTab"] = m_activeTabNumber;
    sessionObject["untitledTabs"] = m_untitledTabs;
    
    QJsonDocument doc(sessionObject);
    
    QSaveFile saveFile(m_sessionFilePath);
    if (saveFile.open(QIODevice::WriteOnly)) {
        saveFile.write(doc.toJson());
        saveFile.commit();
    }
}

void SessionManager::loadSessionFile()
{
    if (m_sessionDir.isEmpty() || !QFile::exists(m_sessionFilePath)) {
        return;
    }
    
    QFile file(m_sessionFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return;
    }
    
    QJsonObject sessionObject = doc.object();
    
    // Load active tab
    m_activeTabNumber = sessionObject["activeTab"].toInt(0);
    
    // Load untitled tabs
    if (sessionObject.contains("untitledTabs") && sessionObject["untitledTabs"].isArray()) {
        m_untitledTabs = sessionObject["untitledTabs"].toArray();
        
        // Rebuild backup files map from loaded data
        m_backupFiles.clear();
        for (int i = 0; i < m_untitledTabs.size(); ++i) {
            QJsonObject tabObj = m_untitledTabs[i].toObject();
            int tabNumber = tabObj["tabNumber"].toInt();
            QString backupPath = tabObj["backupPath"].toString();
            
            // Only add to backup files map if both values are valid
            if (tabNumber > 0 && !backupPath.isEmpty()) {
                m_backupFiles[tabNumber] = backupPath;
            }
        }
    }
}

int SessionManager::getActiveTabNumber() const
{
    return m_activeTabNumber;
}

QJsonArray SessionManager::getUntitledTabs() const
{
    return m_untitledTabs;
}
