#ifndef CLIENTCONNECTION_H
#define CLIENTCONNECTION_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QHash>
#include <QVariantMap>
#include <QJsonObject>
#include <QFutureWatcher>

#include <variant>
#include <QBasicTimer>
#include <QTimerEvent>

class QWebSocket;
class DatabaseWorker;
class RoomManager;
class RateLimiter;

class ClientConnection : public QObject
{
    Q_OBJECT
public:
    explicit ClientConnection(QWebSocket *socket, QObject *parent = nullptr);
    ~ClientConnection();

    void sendMessage(const QJsonObject &message);
    void setAuthenticated(int userId, const QString &username);
    void disconnect();

    void setDatabaseWorker(DatabaseWorker *dbWorker);
    void setRoomManager(RoomManager *roomManager) { m_roomManager = roomManager; }
    void setRateLimiter(RateLimiter *rateLimiter) { m_rateLimiter = rateLimiter; }
    void setTimeouts(int authTimeoutMs, int idleTimeoutMs);
    void setMaxOutboundQueueBytes(qint64 bytes) { m_maxOutboundQueueBytes = qMax<qint64>(1024, bytes); }

    QString temporaryId() const { return m_temporaryId; }
    QString userId() const { return m_userId; }
    QString username() const { return m_username; }
    QString peerAddress() const { return m_peerAddress; }
    bool isAuthenticated() const { return m_authenticated; }

signals:
    void disconnected(const QString &temporaryId);
    void messageReceived(const QJsonObject &message, ClientConnection *client);
    void authenticated(ClientConnection *client);
    void privateMessageRequested(ClientConnection *client, const QString &recipient, const QString &content);
    void conversationRequested(ClientConnection *requester, const QString &partner, int limit, int beforeMessageId);
    void presenceSubscribeRequested(ClientConnection *client);
    void markReadRequested(ClientConnection *client, const QString &with, int beforeMessageId);
    void unreadSummaryRequested(ClientConnection *client);
    void editMessageRequested(ClientConnection *client, int messageId, const QString &newContent);
    void deleteMessageRequested(ClientConnection *client, int messageId);
    void roomMessageSent();
    void registrationSucceeded();
    void authenticationFailed();

protected:
    void timerEvent(QTimerEvent *event) override;

private slots:
    void onTextMessageReceived(const QString &message);
    void onDisconnected();
    void onAuthTaskFinished();
    void onDbRequestFinished(quint64 requestId, int operation, const QVariantMap &result);

private:
    enum class AuthTask
    {
        None,
        RegisterHash,
        VerifyPassword,
        UpgradeHash
    };

    void processMessage(const QJsonObject &message);
    void handleAuthMessage(const QJsonObject &message);
    void handleChatMessage(const QJsonObject &message);
    void handleRegisterMessage(const QJsonObject &message);
    void handlePrivateMessage(const QJsonObject &message);
    void handleConversationRequest(const QJsonObject &message);
    void handleJoinRoom(const QJsonObject &message);
    void handleLeaveRoom(const QJsonObject &message);
    void handleHistoryRequest(const QJsonObject &message);
    void handlePresenceSubscribe(const QJsonObject &message);
    void handleMarkRead(const QJsonObject &message);
    void handleUnreadRequest(const QJsonObject &message);
    void handleEditMessage(const QJsonObject &message);
    void handleDeleteMessage(const QJsonObject &message);

    void startRegistrationHash(const QString &username, const QString &password);
    void startPasswordVerification(const QString &username, const QString &password, const QString &storedHash, int userId);
    void startLegacyHashUpgrade(int userId, const QString &password, bool gateAuthCompletion);
    void completeAuthentication(int userId, const QString &username);
    void recordAuthFailure(const QString &username, const QString &errorMessage);
    void clearPendingAuthSecrets();

    quint64 submitDbRequest(int operation, const QVariantMap &params, const QVariantMap &context);

    QWebSocket *m_socket;
    QBasicTimer m_authTimer;
    QBasicTimer m_idleTimer;

    QString m_temporaryId;
    QString m_userId;
    QString m_username;
    QString m_peerAddress;
    bool m_authenticated;

    int m_authTimeoutMs = 30 * 1000;
    int m_idleTimeoutMs = 5 * 60 * 1000;
    qint64 m_maxOutboundQueueBytes = 1024 * 1024;

    DatabaseWorker *m_dbWorker;
    RoomManager *m_roomManager;
    RateLimiter *m_rateLimiter;

    using AuthWorkerResult = std::variant<QString, bool>;
    QFutureWatcher<AuthWorkerResult> m_authWatcher;
    AuthTask m_authTask = AuthTask::None;
    bool m_authInFlight = false;
    QString m_pendingUsername;
    QString m_pendingPassword;
    QString m_pendingStoredHash;
    int m_pendingUserId = -1;
    bool m_pendingGateAuthCompletion = false;

    QHash<quint64, QVariantMap> m_pendingDb;
};

#endif // CLIENTCONNECTION_H
