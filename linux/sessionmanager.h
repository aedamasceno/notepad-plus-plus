#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QMap>

class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager();

    void saveSession();
    void loadSession();
    void clearSession();
    
    // For handling untitled documents
    void addUntitledDocument(const QString& content, int tabNumber);
    void removeUntitledDocument(int tabNumber);
    void updateUntitledDocumentContent(int tabNumber, const QString& content);
    bool hasUntitledDocuments() const;
    
    // For application shutdown
    void shutdown();
    
    // Accessors for session data
    int getActiveTabNumber() const;
    QJsonArray getUntitledTabs() const;

private:
    QString getSessionDir() const;
    QString getBackupFilePath(int tabNumber) const;
    QString getSessionFilePath() const;
    
    void saveSessionFile();
    void loadSessionFile();
    
    QTimer* m_backupTimer;
    bool m_shouldSaveSession;
    int m_activeTabNumber;
    QString m_sessionDir;
    QString m_sessionFilePath;
    QJsonArray m_untitledTabs;
    QMap<int, QString> m_backupFiles;
};

#endif // SESSIONMANAGER_H
