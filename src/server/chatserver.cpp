#include "ChatServer.h"
#include "ClientConnection.h"
#include "RoomManager.h"
#include "DatabaseWorker.h"
#include "MessageProtocol.h"
#include "RateLimiter.h"
#include "healthserver.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QThread>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslError>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>

namespace {

QString privateKeyPermissionError(const QString &keyPath)
{
#ifdef Q_OS_WIN
    // Qt's permission mapping on Windows does not reliably mirror the real
    // ACL. A full check would use GetNamedSecurityInfo; on Windows the
    // deployer should harden the ACL with icacls instead.
    Q_UNUSED(keyPath);
    return {};
#else
    const QFileInfo info(keyPath);
    const QFileDevice::Permissions forbidden =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

    if (info.permissions() & forbidden)
    {
        return QString("Private key %1 is readable/writable by group or others").arg(keyPath);
    }
    return {};
#endif
}

} // namespace

ChatServer::ChatServer(const ServerConfig &config, QObject *parent)
    : QObject(parent)
    , m_config{config}
{
    m_roomManager = std::make_unique<RoomManager>();
    m_presence = std::make_unique<PresenceManager>();
    m_rateLimiter = std::make_unique<RateLimiter>();

    connect(m_presence.get(), &PresenceManager::presenceChanged, this, &ChatServer::onPresenceChanged);
}

ChatServer::~ChatServer()
{
    stop();
}

bool ChatServer::startDatabaseWorker()
{
    if (m_dbWorker && m_dbThread)
    {
        return m_dbWorker->databaseHealthy();
    }

    m_dbThread = new QThread(this);
    m_dbThread->setObjectName(QStringLiteral("DatabaseWorkerThread"));

    m_dbWorker = new DatabaseWorker();
    m_dbWorker->moveToThread(m_dbThread);
    m_dbThread->start();

    bool initialized = false;
    const bool invoked = QMetaObject::invokeMethod(
        m_dbWorker, "initialize", Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(bool, initialized),
        Q_ARG(QString, m_config.databasePath()),
        Q_ARG(bool, m_config.seedDemoUser()));

    if (!invoked || !initialized)
    {
        qCritical() << "Database worker initialization failed";
        shutdownDatabaseWorker();
        return false;
    }

    connect(m_dbWorker, &DatabaseWorker::requestFinished, this, &ChatServer::onDbRequestFinished);

    return true;
}

void ChatServer::shutdownDatabaseWorker()
{
    if (m_dbWorker)
    {
        disconnect(m_dbWorker, nullptr, this, nullptr);
        QMetaObject::invokeMethod(m_dbWorker, "close", Qt::BlockingQueuedConnection);
    }

    if (m_dbThread)
    {
        m_dbThread->quit();
        if (!m_dbThread->wait(5000))
        {
            qWarning() << "Database worker thread did not stop in time";
        }
        delete m_dbThread;
        m_dbThread = nullptr;
    }

    delete m_dbWorker;
    m_dbWorker = nullptr;

    m_pendingDb.clear();
}

quint64 ChatServer::submitDbRequest(int operation, const QVariantMap &params, ClientConnection *client, const QVariantMap &context)
{
    if (!m_dbWorker)
    {
        return 0;
    }

    const quint64 requestId = DatabaseWorker::nextRequestId();
    PendingDbCall call;
    call.operation = operation;
    call.client = client;
    call.context = context;
    m_pendingDb.insert(requestId, call);

    QMetaObject::invokeMethod(m_dbWorker, "submitRequest", Qt::QueuedConnection,
                              Q_ARG(quint64, requestId),
                              Q_ARG(int, operation),
                              Q_ARG(QVariantMap, params));
    return requestId;
}

bool ChatServer::start()
{
    if (m_running)
    {
        qWarning() << "Server is already running";
        return true;
    }

    if (!startDatabaseWorker())
    {
        return false;
    }

    m_rateLimiter->setLimits(m_config.authFailureLimit(),m_config.authIpFailureLimit(),  m_config.registrationRateLimitPerMinute());

    const QWebSocketServer::SslMode mode =
        m_config.tlsEnabled() ? QWebSocketServer::SecureMode : QWebSocketServer::NonSecureMode;

    m_webSocketServer = new QWebSocketServer(QStringLiteral("chat-server"), mode, this);

    if (m_config.tlsEnabled())
    {
        QFile certFile{m_config.certPath()};
        if (!certFile.open(QIODevice::ReadOnly))
        {
            qCritical() << "Failed to open certificate" << m_config.certPath() << "-" << certFile.errorString();
            return false;
        }
        QSslCertificate certificate{&certFile, QSsl::Pem};
        certFile.close();

        QFile keyFile{m_config.keyPath()};
        if (!keyFile.open(QIODevice::ReadOnly))
        {
            qCritical() << "Failed to open private key" << m_config.keyPath() << "-" << keyFile.errorString();
            return false;
        }
        QSslKey key{&keyFile, QSsl::Rsa, QSsl::Pem};
        keyFile.close();

        if (certificate.isNull())
        {
            qCritical() << "Failed to parse certificate at:" << m_config.certPath();
            return false;
        }
        if (key.isNull())
        {
            qCritical() << "Failed to parse private key at:" << m_config.keyPath();
            return false;
        }

        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        if (nowUtc < certificate.effectiveDate().toUTC()
            || nowUtc > certificate.expiryDate().toUTC())
        {
            qCritical() << "TLS certificate is expired or not yet valid:"
                        << m_config.certPath();
            return false;
        }

        if (m_config.requirePrivateKeyPermissions())
        {
            const QString permissionError = privateKeyPermissionError(m_config.keyPath());
            if (!permissionError.isEmpty())
            {
                qCritical() << permissionError;
                return false;
            }
        }

        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setLocalCertificate(certificate);
        sslConfig.setPrivateKey(key);
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);

        m_webSocketServer->setSslConfiguration(sslConfig);

        connect(m_webSocketServer, &QWebSocketServer::sslErrors, this,
                [](const QList<QSslError> &errors)
                {
                    for (const auto &err : errors)
                    {
                        qWarning() << "Server TLS error:" << err.errorString();
                    }
                });
    }

    if (!m_webSocketServer->listen(QHostAddress::Any, m_config.port()))
    {
        qCritical() << "Failed to listen on port" << m_config.port() << ":" << m_webSocketServer->errorString();
        return false;
    }

    connect(m_webSocketServer, &QWebSocketServer::newConnection, this, &ChatServer::onNewConnection);

    if (m_config.healthEnabled())
    {
        m_healthServer = std::make_unique<HealthServer>(m_config.healthPort(), this);
        if (!m_healthServer->start())
        {
            qWarning() << "Health server failed to start â€” continuing without it";
            m_healthServer.reset();
        }
    }

    m_running = true;
    m_stopping = false;
    qInfo() << "Chat server started on port" << m_config.port() << (m_config.tlsEnabled() ? "(wss / TLS)" : "(ws / plaintext)");

    return true;
}

void ChatServer::stop()
{
    if (m_running)
    {
        m_running = false;
        m_stopping = true;

        if (m_webSocketServer)
        {
            m_webSocketServer->close();
        }

        const QList<ClientConnection *> clients = m_clients.values();
        for (ClientConnection *client : clients)
        {
            if (client)
            {
                client->sendMessage(MessageProtocol::createErrorMessage(503, "Server shutting down"));
                client->disconnect();
            }
        }

        for (ClientConnection *client : clients)
        {
            delete client;
        }

        m_clients.clear();
        m_connections.clear();
        m_connectionsPerIp.clear();

        if (m_roomManager)
        {
            m_roomManager->clear();
        }
        if (m_presence)
        {
            m_presence->clear();
        }
        if (m_rateLimiter)
        {
            m_rateLimiter->clear();
        }

        if (m_webSocketServer)
        {
            delete m_webSocketServer;
            m_webSocketServer = nullptr;
        }

        if (m_healthServer)
        {
            m_healthServer->stop();
            m_healthServer.reset();
        }

        m_stopping = false;
        qInfo() << "Chat server stopped";
    }

    shutdownDatabaseWorker();
}

bool ChatServer::isDatabaseHealthy() const
{
    return m_dbWorker && m_dbWorker->databaseHealthy();
}

void ChatServer::onNewConnection()
{
    QWebSocket *socket = m_webSocketServer->nextPendingConnection();

    if (!socket)
    {
        qWarning() << "Failed to get pending connection";
        return;
    }

    socket->setMaxAllowedIncomingFrameSize(m_config.maxIncomingFrameSize());
    socket->setMaxAllowedIncomingMessageSize(m_config.maxIncomingMessageSize());

    if (m_clients.size() >= m_config.maxConnections())
    {
        qWarning() << "Rejecting connection: global limit reached";
        socket->close();
        socket->deleteLater();
        return;
    }

    const QString ip = socket->peerAddress().toString();
    const int currentFromIp = m_connectionsPerIp.value(ip, 0);
    if (currentFromIp >= m_config.maxConnectionsPerIp())
    {
        qWarning() << "Rejecting connection from" << ip << ": per-IP limit reached";
        socket->close();
        socket->deleteLater();
        return;
    }

    if (currentFromIp <= 0)
    {
        m_connectionsPerIp.remove(ip);
    }
    m_connectionsPerIp.insert(ip, currentFromIp + 1);

    auto *client = new ClientConnection(socket, this);
    socket->setParent(client);

    client->setDatabaseWorker(m_dbWorker);
    client->setRoomManager(m_roomManager.get());
    client->setRateLimiter(m_rateLimiter.get());
    client->setTimeouts(m_config.authTimeoutSeconds() * 1000,
                        m_config.idleTimeoutSeconds() * 1000);
    client->setMaxOutboundQueueBytes(m_config.maxOutboundQueueBytes());

    connect(client, &ClientConnection::disconnected, this, &ChatServer::onClientDisconnected);
    connect(client, &ClientConnection::authenticated, this, &ChatServer::onClientAuthenticated);
    connect(client, &ClientConnection::privateMessageRequested, this, &ChatServer::onPrivateMessageRequested);
    connect(client, &ClientConnection::conversationRequested, this, &ChatServer::onConversationRequested);
    connect(client, &ClientConnection::presenceSubscribeRequested, this, &ChatServer::onPresenceSubscribeRequested);
    connect(client, &ClientConnection::markReadRequested, this, &ChatServer::onMarkReadRequested);
    connect(client, &ClientConnection::unreadSummaryRequested, this, &ChatServer::onUnreadSummaryRequested);
    connect(client, &ClientConnection::editMessageRequested, this, &ChatServer::onEditMessageRequested);
    connect(client, &ClientConnection::deleteMessageRequested, this, &ChatServer::onDeleteMessageRequested);
    connect(client, &ClientConnection::registrationSucceeded, this, [this]() { ++m_registrations; });
    connect(client, &ClientConnection::authenticationFailed, this, [this]() { ++m_authFailures; });
    connect(client, &ClientConnection::roomMessageSent, this, [this]() { ++m_messagesSent; });

    m_clients.insert(client->temporaryId(), client);

    qInfo() << "New client connected from" << ip << "Temporary ID:" << client->temporaryId();
}

void ChatServer::onClientDisconnected(const QString &temporaryId)
{
    ClientConnection *client = nullptr;

    auto it = m_clients.find(temporaryId);
    if (it == m_clients.end())
    {
        return;
    }

    client = it.value();
    m_clients.erase(it);

    // Drop DB requests belonging to this connection.
    for (auto pending = m_pendingDb.begin(); pending != m_pendingDb.end();)
    {
        if (pending.value().client == client)
        {
            pending = m_pendingDb.erase(pending);
        }
        else
        {
            ++pending;
        }
    }

    if (!client->peerAddress().isEmpty())
    {
        const int count = m_connectionsPerIp.value(client->peerAddress(), 0);
        if (count > 1)
        {
            m_connectionsPerIp.insert(client->peerAddress(), count - 1);
        }
        else
        {
            m_connectionsPerIp.remove(client->peerAddress());
        }
    }

    if (client->isAuthenticated())
    {
        m_presence->handleUserDisconnected(client->userId(), client);
        m_connections.remove(client->userId(), client);
        if (m_roomManager)
        {
            m_roomManager->removeClientFromAllRooms(client);
        }
    }

    if (!m_stopping)
    {
        client->deleteLater();
    }
}

void ChatServer::onClientAuthenticated(ClientConnection *client)
{
    if (!client)
    {
        return;
    }

    m_connections.insert(client->userId(), client);
    qInfo() << "Client authenticated:" << client->username() << "(ID:" << client->userId() << ")";
    m_presence->handleUserConnected(client->userId(), client);
}

void ChatServer::onPrivateMessageRequested(ClientConnection *sender, const QString &recipientName, const QString &content)
{
    if (!sender || !sender->isAuthenticated())
    {
        return;
    }

    if (content.trimmed().isEmpty())
    {
        sender->sendMessage(MessageProtocol::createErrorMessage(400, "Empty message"));
        return;
    }

    if (content.size() > 10000)
    {
        sender->sendMessage(MessageProtocol::createErrorMessage(400, "Message too long"));
        return;
    }

    if (recipientName == sender->username())
    {
        sender->sendMessage(MessageProtocol::createErrorMessage(400, "Cannot message yourself"));
        return;
    }

    QVariantMap params;
    params["senderId"] = sender->userId().toInt();
    params["senderName"] = sender->username();
    params["recipientName"] = recipientName;
    params["content"] = content;
    params["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QVariantMap context;
    context["content"] = content;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::SendPrivateMessage), params, sender, context);
}

void ChatServer::onConversationRequested(ClientConnection *requester, const QString &partner, int limit, int beforeMessageId)
{
    if (!requester || !requester->isAuthenticated())
    {
        return;
    }

    QVariantMap params;
    params["requesterId"] = requester->userId().toInt();
    params["partnerName"] = partner;
    params["limit"] = qBound(1, limit, 200);
    params["beforeMessageId"] = qMax(-1, beforeMessageId);

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::GetConversation), params, requester, QVariantMap{});
}

void ChatServer::onPresenceSubscribeRequested(ClientConnection *client)
{
    if (!client || !client->isAuthenticated())
    {
        return;
    }

    QJsonArray users;

    for (const QString &userId : m_presence->onlineUserIds())
    {
        const auto connection = m_connections.values(userId);
        if (!connection.isEmpty() && connection.first())
        {
            QJsonObject u;
            u["userId"] = userId;
            u["username"] = connection.first()->username();
            u["online"] = true;
            u["last_seen"] = 0;
            users.append(u);
        }
    }
    client->sendMessage(MessageProtocol::createPresenceListMessage(users));
}

void ChatServer::onPresenceChanged(const QString &userId, bool online, qint64 lastSeen)
{
    QString username;
    const auto connection = m_connections.values(userId);
    if (!connection.isEmpty() && connection.first())
    {
        username = connection.first()->username();
    }

    if (!username.isEmpty())
    {
        const QJsonObject update = MessageProtocol::createPresenceUpdateMessage(userId, username, online, lastSeen);
        for (ClientConnection *connection : std::as_const(m_clients))
        {
            if (connection && connection->isAuthenticated())
            {
                connection->sendMessage(update);
            }
        }
        return;
    }

    // Offline lookup is asynchronous now that SQLite lives on the worker thread.
    QVariantMap context;
    context["userId"] = userId;
    context["online"] = online;
    context["lastSeen"] = lastSeen;

    QVariantMap params;
    params["userId"] = userId.toInt();

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::GetUsernameById), params, nullptr, context);
}

void ChatServer::onMarkReadRequested(ClientConnection *client, const QString &with, int beforeMessageId)
{
    if (!client || !client->isAuthenticated())
    {
        return;
    }

    QVariantMap params;
    params["recipientId"] = client->userId().toInt();
    params["withUsername"] = with;
    params["beforeMessageId"] = qMax(-1, beforeMessageId);

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::MarkRead), params, client, QVariantMap{});
}

void ChatServer::onUnreadSummaryRequested(ClientConnection *client)
{
    if (!client || !client->isAuthenticated())
    {
        return;
    }

    QVariantMap params;
    params["recipientId"] = client->userId().toInt();

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::GetUnreadSummary), params, client, QVariantMap{});
}

void ChatServer::onEditMessageRequested(ClientConnection *client, int messageId, const QString &newContent)
{
    if (!client || !client->isAuthenticated())
    {
        return;
    }

    if (newContent.isEmpty() || newContent.size() > 10000)
    {
        client->sendMessage(MessageProtocol::createErrorMessage(400, "Invalid content"));
        return;
    }

    QVariantMap params;
    params["messageId"] = messageId;
    params["userId"] = client->userId().toInt();
    params["content"] = newContent;

    QVariantMap context;
    context["messageId"] = messageId;
    context["newContent"] = newContent;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::UpdateMessage), params, client, context);
}

void ChatServer::onDeleteMessageRequested(ClientConnection *client, int messageId)
{
    if (!client || !client->isAuthenticated())
    {
        return;
    }

    QVariantMap params;
    params["messageId"] = messageId;
    params["userId"] = client->userId().toInt();

    QVariantMap context;
    context["messageId"] = messageId;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::DeleteMessage), params, client, context);
}

void ChatServer::onDbRequestFinished(quint64 requestId, int operation, const QVariantMap &result)
{
    auto it = m_pendingDb.find(requestId);
    if (it == m_pendingDb.end())
    {
        return;
    }

    const PendingDbCall call = it.value();
    m_pendingDb.erase(it);

    ClientConnection *client = call.client.data();
    const auto op = static_cast<DatabaseWorker::Operation>(operation);

    switch (op)
    {
    case DatabaseWorker::Operation::SendPrivateMessage:
    {
        if (!client || !client->isAuthenticated())
        {
            return;
        }

        if (!result.value("success", false).toBool())
        {
            if (result.value("notFound", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(404, "User not found"));
            }
            else
            {
                client->sendMessage(MessageProtocol::createErrorMessage(500, "Failed to save message"));
            }
            return;
        }

        ++m_privateMessagesSent;

        const QString recipientName = result.value("recipientName").toString();
        const int recipientId = result.value("recipientId").toInt();
        const int messageId = result.value("messageId").toInt();
        const QDateTime timestamp = QDateTime::fromString(result.value("timestamp").toString(), Qt::ISODate);

        client->sendMessage(MessageProtocol::createPrivateMessageSent(recipientName, messageId, timestamp));

        const QJsonObject delivery = MessageProtocol::createPrivateMessage(
            client->username(), recipientName,
            call.context.value("content").toString(), timestamp, messageId);

        const auto connections = m_connections.values(QString::number(recipientId));
        for (ClientConnection *connection : connections)
        {
            if (connection)
            {
                connection->sendMessage(delivery);
            }
        }
        return;
    }

    case DatabaseWorker::Operation::GetConversation:
    {
        if (!client || !client->isAuthenticated())
        {
            return;
        }

        if (!result.value("success", false).toBool())
        {
            if (result.value("notFound", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(404, "User not found"));
            }
            else
            {
                client->sendMessage(MessageProtocol::createErrorMessage(500, "Database error"));
            }
            return;
        }

        const QString partnerName = result.value("partnerName").toString();
        const int requesterId = client->userId().toInt();

        QJsonArray arr;
        const QVariantList messages = result.value("messages").toList();
        for (const QVariant &value : messages)
        {
            const QVariantMap pm = value.toMap();
            const bool sentByMe = pm.value("senderId").toInt() == requesterId;

            QJsonObject obj;
            obj["id"] = pm.value("id").toInt();
            obj["from"] = sentByMe ? client->username() : partnerName;
            obj["to"] = sentByMe ? partnerName : client->username();
            obj["content"] = pm.value("content").toString();
            obj["timestamp"] = pm.value("timestamp").toString();
            arr.append(obj);
        }

        client->sendMessage(MessageProtocol::createConversationResponse(partnerName, arr));
        return;
    }

    case DatabaseWorker::Operation::MarkRead:
    {
        if (!client || !client->isAuthenticated())
        {
            return;
        }

        if (!result.value("success", false).toBool())
        {
            if (result.value("notFound", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(404, "User not found"));
            }
            else
            {
                client->sendMessage(MessageProtocol::createErrorMessage(500, "Database error"));
            }
            return;
        }

        const int highestRead = result.value("highestRead").toInt();
        if (highestRead > 0)
        {
            const int otherId = result.value("otherId").toInt();
            const QDateTime now = QDateTime::currentDateTimeUtc();
            const QJsonObject receipt = MessageProtocol::createReadReceiptMessage(client->username(), highestRead, now);

            const auto connections = m_connections.values(QString::number(otherId));
            for (ClientConnection *connection : connections)
            {
                if (connection)
                {
                    connection->sendMessage(receipt);
                }
            }
        }

        onUnreadSummaryRequested(client);
        return;
    }

    case DatabaseWorker::Operation::GetUnreadSummary:
    {
        if (!client || !client->isAuthenticated())
        {
            return;
        }

        QJsonArray arr;
        const QVariantList conversations = result.value("conversations").toList();
        for (const QVariant &value : conversations)
        {
            const QVariantMap summary = value.toMap();

            QJsonObject obj;
            obj["with"] = summary.value("senderName").toString();
            obj["count"] = summary.value("count").toInt();
            obj["lastMessageAt"] = summary.value("lastMessageAt").toString();
            arr.append(obj);
        }

        client->sendMessage(MessageProtocol::createUnreadSummaryMessage(arr));
        return;
    }

    case DatabaseWorker::Operation::UpdateMessage:
    {
        if (!client || !client->isAuthenticated())
        {
            return;
        }

        if (!result.value("success", false).toBool())
        {
            if (result.value("notFound", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(404, "Message not found"));
            }
            else if (result.value("forbidden", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(403, "Not your message"));
            }
            else if (result.value("deleted", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(400, "Message is deleted"));
            }
            else
            {
                client->sendMessage(MessageProtocol::createErrorMessage(500, "Failed to update"));
            }
            return;
        }

        const int roomId = result.value("roomId").toInt();
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QJsonObject broadcast = MessageProtocol::createMessageEditedMessage(call.context.value("messageId").toInt(), call.context.value("newContent").toString(), now);

        m_roomManager->broadcastToRoom(QString::number(roomId), broadcast);
        return;
    }

    case DatabaseWorker::Operation::DeleteMessage:
    {
        if (!client || !client->isAuthenticated())
        {
            return;
        }

        if (!result.value("success", false).toBool())
        {
            if (result.value("notFound", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(404, "Message not found"));
            }
            else if (result.value("forbidden", false).toBool())
            {
                client->sendMessage(MessageProtocol::createErrorMessage(403, "Not your message"));
            }
            else
            {
                client->sendMessage(MessageProtocol::createErrorMessage(500, "Failed to delete"));
            }
            return;
        }

        const int roomId = result.value("roomId").toInt();
        const QJsonObject broadcast = MessageProtocol::createMessageDeletedMessage(
            call.context.value("messageId").toInt());
        m_roomManager->broadcastToRoom(QString::number(roomId), broadcast);
        return;
    }

    case DatabaseWorker::Operation::GetUsernameById:
    {
        const QString userId = call.context.value("userId").toString();
        const bool online = call.context.value("online", false).toBool();
        const qint64 lastSeen = call.context.value("lastSeen", 0).toLongLong();
        const QString username = result.value("username").toString();

        const QJsonObject update = MessageProtocol::createPresenceUpdateMessage(userId, username, online, lastSeen);
        for (ClientConnection *connection : std::as_const(m_clients))
        {
            if (connection && connection->isAuthenticated())
            {
                connection->sendMessage(update);
            }
        }
        return;
    }

    default:
        qWarning() << "ChatServer received unexpected DB operation result:" << operation;
        return;
    }
}