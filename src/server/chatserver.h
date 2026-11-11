#ifndef CHATSERVER_H
#define CHATSERVER_H

#include "AuthManager.h"
#include "PresenceManager.h"

#include <QObject>
#include <QMap>
#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <memory>
#include "config/ServerConfig.h"

class QWebSocketServer;
class QWebSocket;
class QThread;
class ClientConnection;
class DatabaseWorker;
class RoomManager;
class HealthServer;
class RateLimiter;

class ChatServer : public QObject
{
    Q_OBJECT
public:
    explicit ChatServer(const ServerConfig &config, QObject *parent = nullptr);
    ~ChatServer();

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    int connectedClientCount() const { return m_clients.size(); }
    bool isDatabaseHealthy() const;

    quint64 messagesSent() const { return m_messagesSent; }
    quint64 privateMessagesSent() const { return m_privateMessagesSent; }
    quint64 authFailures() const { return m_authFailures; }
    quint64 registrations() const { return m_registrations; }

private slots:
    void onNewConnection();
    void onClientDisconnected(const QString &clientId);
    void onClientAuthenticated(ClientConnection *client);

    void onPrivateMessageRequested(ClientConnection *sender, const QString &recipient, const QString &content);
    void onConversationRequested(ClientConnection *requester, const QString &partner, int limit, int beforeMessageId);

    void onPresenceSubscribeRequested(ClientConnection *client);
    void onPresenceChanged(const QString &userId, bool online, qint64 lastSeen);

    void onMarkReadRequested(ClientConnection *client, const QString &with, int beforeMessageId);
    void onUnreadSummaryRequested(ClientConnection *client);

    void onEditMessageRequested(ClientConnection *client, int messageId, const QString &newContent);
    void onDeleteMessageRequested(ClientConnection *client, int messageId);
    void onDbRequestFinished(quint64 requestId, int operation, const QVariantMap &result);

private:
    struct PendingDbCall
    {
        int operation = -1;
        QPointer<ClientConnection> client;
        QVariantMap context;
    };

    bool startDatabaseWorker();
    void shutdownDatabaseWorker();
    quint64 submitDbRequest(int operation, const QVariantMap &params, ClientConnection *client, const QVariantMap &context);

    QWebSocketServer *m_webSocketServer = nullptr;
    QMap<QString, ClientConnection *> m_clients;
    QMultiMap<QString, ClientConnection *> m_connections;
    QHash<QString, int> m_connectionsPerIp;

    QThread *m_dbThread = nullptr;
    DatabaseWorker *m_dbWorker = nullptr;

    std::unique_ptr<RoomManager> m_roomManager;
    std::unique_ptr<PresenceManager> m_presence;
    std::unique_ptr<RateLimiter> m_rateLimiter;
    std::unique_ptr<HealthServer> m_healthServer;

    QHash<quint64, PendingDbCall> m_pendingDb;

    ServerConfig m_config;
    bool m_running = false;
    bool m_stopping = false;

    quint64 m_messagesSent = 0;
    quint64 m_privateMessagesSent = 0;
    quint64 m_authFailures = 0;
    quint64 m_registrations = 0;
};

#endif // CHATSERVER_H