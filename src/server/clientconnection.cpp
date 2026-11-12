#include "clientconnection.h"
#include "messageprotocol.h"
#include "databaseworker.h"
#include "roommanager.h"
#include "authmanager.h"
#include "PasswordHasher.h"
#include "ratelimiter.h"

#include <QWebSocket>
#include <chrono>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVariantList>
#include <QDebug>
#include <QDateTime>
#include <QtConcurrent>

#include <algorithm>

ClientConnection::ClientConnection(QWebSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
    , m_temporaryId(QUuid::createUuid().toString())
    , m_peerAddress(socket ? socket->peerAddress().toString() : QString())
    , m_authenticated(false)
    , m_dbWorker(nullptr)
    , m_roomManager(nullptr)
    , m_rateLimiter(nullptr)
{
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ClientConnection::onTextMessageReceived);
    connect(m_socket, &QWebSocket::disconnected, this, &ClientConnection::onDisconnected);
    connect(&m_authWatcher, &QFutureWatcher<AuthWorkerResult>::finished, this, &ClientConnection::onAuthTaskFinished);

    m_authTimer.start(std::chrono::milliseconds(m_authTimeoutMs), this);
}

ClientConnection::~ClientConnection()
{
    if (m_socket)
    {
        m_socket->disconnect(this);
        m_socket->close();
    }
    m_socket = nullptr;
}

void ClientConnection::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_authTimer.timerId())
    {
        sendMessage(MessageProtocol::createErrorMessage(408, "Authentication timeout"));
        disconnect();
        return;
    }

    if (event->timerId() == m_idleTimer.timerId())
    {
        sendMessage(MessageProtocol::createErrorMessage(408, "Idle timeout"));
        disconnect();
        return;
    }

    QObject::timerEvent(event);
}

void ClientConnection::setDatabaseWorker(DatabaseWorker *dbWorker)
{
    if (m_dbWorker == dbWorker)
    {
        return;
    }

    if (m_dbWorker)
    {
        QObject::disconnect(m_dbWorker, &DatabaseWorker::requestFinished,this, &ClientConnection::onDbRequestFinished);
    }

    m_dbWorker = dbWorker;

    if (m_dbWorker)
    {
        connect(m_dbWorker, &DatabaseWorker::requestFinished, this, &ClientConnection::onDbRequestFinished, Qt::UniqueConnection);
    }
}

void ClientConnection::setTimeouts(int authTimeoutMs, int idleTimeoutMs)
{
    m_authTimeoutMs = qMax(1000, authTimeoutMs);
    m_idleTimeoutMs = qMax(1000, idleTimeoutMs);

    if (!m_authenticated)
    {
        m_authTimer.start(std::chrono::milliseconds(m_authTimeoutMs), this);
    }
    else
    {
        m_idleTimer.start(std::chrono::milliseconds(m_idleTimeoutMs), this);
    }
}

quint64 ClientConnection::submitDbRequest(int operation, const QVariantMap &params, const QVariantMap &context)
{
    if (!m_dbWorker)
    {
        return 0;
    }

    const quint64 requestId = DatabaseWorker::nextRequestId();
    m_pendingDb.insert(requestId, context);

    QMetaObject::invokeMethod(m_dbWorker, "submitRequest", Qt::QueuedConnection,
                              Q_ARG(quint64, requestId),
                              Q_ARG(int, operation),
                              Q_ARG(QVariantMap, params));
    return requestId;
}

void ClientConnection::sendMessage(const QJsonObject &message)
{
    if (!m_socket || !m_socket->isValid())
    {
        return;
    }

    QJsonDocument doc(message);
    const QByteArray payload = doc.toJson(QJsonDocument::Compact);

    const qint64 alreadyQueued = qMax<qint64>(0, m_socket->bytesToWrite());
    if (alreadyQueued + payload.size() > m_maxOutboundQueueBytes)
    {
        qWarning() << "Closing slow client" << m_peerAddress
                   << "outbound queue exceeded"
                   << m_maxOutboundQueueBytes << "bytes";
        m_socket->close();
        return;
    }

    m_socket->sendTextMessage(QString::fromUtf8(payload));
}

void ClientConnection::setAuthenticated(int userId, const QString &username)
{
    m_userId = QString::number(userId);
    m_username = username;
    m_authenticated = true;
}

void ClientConnection::disconnect()
{
    if (m_socket)
    {
        m_socket->close();
    }
}

void ClientConnection::clearPendingAuthSecrets()
{
    // QString cannot be zeroed reliably due to implicit sharing, but release
    // our references to password material as soon as auth work is complete.
    m_pendingPassword.clear();
    m_pendingStoredHash.clear();
}

void ClientConnection::recordAuthFailure(const QString &username, const QString &errorMessage)
{
    const QString rateKey = username.left(64);

    if (m_rateLimiter)
    {
        m_rateLimiter->recordAuthFailure(rateKey, m_peerAddress);
    }

    sendMessage(MessageProtocol::createErrorMessage(401, errorMessage));
    qWarning() << "Authentication failed for user:" << rateKey << "-" << errorMessage;
    emit authenticationFailed();
}

void ClientConnection::onTextMessageReceived(const QString &message)
{
    if (m_authenticated)
    {
        m_idleTimer.start(std::chrono::milliseconds(m_idleTimeoutMs), this);
    }
    else
    {
        m_authTimer.start(std::chrono::milliseconds(m_authTimeoutMs), this);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);

    if (error.error != QJsonParseError::NoError)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Invalid JSON format"));
        return;
    }

    QJsonObject jsonMessage = doc.object();

    if (!MessageProtocol::isValidMessage(jsonMessage))
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Invalid message format"));
        return;
    }

    processMessage(jsonMessage);
}

void ClientConnection::onDisconnected()
{
    m_authTimer.stop();
    m_idleTimer.stop();
    emit disconnected(m_temporaryId);
}

void ClientConnection::processMessage(const QJsonObject &message)
{
    if (m_authInFlight)
    {
        sendMessage(MessageProtocol::createErrorMessage(429, "Request already in progress"));
        return;
    }

    MessageProtocol::MessageType type = MessageProtocol::getMessageType(message);

    switch (type)
    {
    case MessageProtocol::MessageType::Auth:
        handleAuthMessage(message);
        break;

    case MessageProtocol::MessageType::Register:
        handleRegisterMessage(message);
        break;

    case MessageProtocol::MessageType::PrivateMessage:
        if (m_authenticated)
        {
            handlePrivateMessage(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::ConversationRequest:
        if (m_authenticated)
        {
            handleConversationRequest(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::Message:
        if (m_authenticated)
        {
            handleChatMessage(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::JoinRoom:
        if (m_authenticated)
        {
            handleJoinRoom(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::LeaveRoom:
        if (m_authenticated)
        {
            handleLeaveRoom(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::HistoryRequest:
        if (m_authenticated)
        {
            handleHistoryRequest(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::PresenceSubscribe:
        if (m_authenticated)
        {
            handlePresenceSubscribe(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::PresenceUpdate:
    case MessageProtocol::MessageType::PresenceList:
        sendMessage(MessageProtocol::createErrorMessage(400, "Server-only message type"));
        break;

    case MessageProtocol::MessageType::MarkRead:
        if (m_authenticated)
        {
            handleMarkRead(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::UnreadRequest:
        if (m_authenticated)
        {
            handleUnreadRequest(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::ReadReceipt:
    case MessageProtocol::MessageType::UnreadSummary:
        sendMessage(MessageProtocol::createErrorMessage(400, "Server-only message type"));
        break;

    case MessageProtocol::MessageType::EditMessage:
        if (m_authenticated)
        {
            handleEditMessage(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::DeleteMessage:
        if (m_authenticated)
        {
            handleDeleteMessage(message);
        }
        else
        {
            sendMessage(MessageProtocol::createErrorMessage(401, "Not authenticated"));
        }
        break;

    case MessageProtocol::MessageType::MessageEdited:
    case MessageProtocol::MessageType::MessageDeleted:
        sendMessage(MessageProtocol::createErrorMessage(400, "Server-only message type"));
        break;

    default:
        sendMessage(MessageProtocol::createErrorMessage(400, "Unsupported message type"));
        break;
    }
}

void ClientConnection::handleAuthMessage(const QJsonObject &message)
{
    if (m_authenticated)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Already authenticated"));
        return;
    }

    const QString username = MessageProtocol::getUsername(message);
    const QString password = MessageProtocol::getPassword(message);

    if (username.isEmpty() || password.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Username and password required"));
        return;
    }

    if (username.length() > 32 || password.length() > 512)
    {
        recordAuthFailure(username, "Invalid credentials");
        return;
    }

    if (m_rateLimiter && m_rateLimiter->isAuthBlocked(username, m_peerAddress))
    {
        sendMessage(MessageProtocol::createErrorMessage(429, "Too many failed attempts"));
        emit authenticationFailed();
        return;
    }

    m_authInFlight = true;
    m_pendingUsername = username;
    m_pendingPassword = password;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::FindUserForAuth),
                    QVariantMap{{"username", username}},
                    QVariantMap{{"stage", "auth_lookup"}});
}

void ClientConnection::startPasswordVerification(const QString &username, const QString &password, const QString &storedHash, int userId)
{
    m_authTask = AuthTask::VerifyPassword;
    m_authInFlight = true;
    m_pendingUsername = username;
    m_pendingPassword = password;
    m_pendingStoredHash = storedHash;
    m_pendingUserId = userId;

    m_authWatcher.setFuture(QtConcurrent::run(&PasswordHasher::pool(), [password, storedHash]() -> AuthWorkerResult {
        return PasswordHasher::verify(password, storedHash);
    }));
}

void ClientConnection::handleRegisterMessage(const QJsonObject &message)
{
    if (m_authenticated)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Already authenticated"));
        return;
    }

    const QString username = MessageProtocol::getUsername(message);
    const QString password = MessageProtocol::getPassword(message);

    if (username.isEmpty() || password.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Username and password required"));
        return;
    }

    const QString validationError = AuthManager::validateRegistration(username, password);
    if (!validationError.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, validationError));
        return;
    }

    if (m_rateLimiter && !m_rateLimiter->registerAttempt(m_peerAddress))
    {
        sendMessage(MessageProtocol::createErrorMessage(429, "Too many registrations"));
        return;
    }

    startRegistrationHash(username, password);
}

void ClientConnection::startRegistrationHash(const QString &username, const QString &password)
{
    m_authTask = AuthTask::RegisterHash;
    m_authInFlight = true;
    m_pendingUsername = username;
    m_pendingPassword = password;

    m_authWatcher.setFuture(QtConcurrent::run(&PasswordHasher::pool(), [password]() -> AuthWorkerResult {
        return PasswordHasher::hash(password);
    }));
}

void ClientConnection::startLegacyHashUpgrade(int userId, const QString &password, bool gateAuthCompletion)
{
    m_authTask = AuthTask::UpgradeHash;
    m_authInFlight = gateAuthCompletion;
    m_pendingUserId = userId;
    m_pendingPassword = password;
    m_pendingGateAuthCompletion = gateAuthCompletion;

    m_authWatcher.setFuture(QtConcurrent::run(&PasswordHasher::pool(), [password]() -> AuthWorkerResult {
        return PasswordHasher::hash(password);
    }));
}

void ClientConnection::onAuthTaskFinished()
{
    const AuthTask task = m_authTask;
    const AuthWorkerResult result = m_authWatcher.result();
    m_authTask = AuthTask::None;
    m_authInFlight = false;

    if (task == AuthTask::None)
    {
        return;
    }

    if (task == AuthTask::RegisterHash)
    {
        const QString passwordHash = std::get<QString>(result);
        clearPendingAuthSecrets();

        if (passwordHash.isEmpty())
        {
            sendMessage(MessageProtocol::createErrorMessage(500, "Password hashing failed"));
            return;
        }

        m_authInFlight = true;
        submitDbRequest(static_cast<int>(DatabaseWorker::Operation::RegisterUser),
                        QVariantMap{{"username", m_pendingUsername},
                                    {"passwordHash", passwordHash}},
                        QVariantMap{{"stage", "register"}});
        return;
    }

    if (task == AuthTask::VerifyPassword)
    {
        if (!std::get<bool>(result))
        {
            clearPendingAuthSecrets();
            recordAuthFailure(m_pendingUsername, "Invalid credentials");
            return;
        }

        if (m_rateLimiter)
        {
            m_rateLimiter->recordAuthSuccess(m_pendingUsername, m_peerAddress);
        }

        if (!PasswordHasher::isArgon2Hash(m_pendingStoredHash))
        {
            startLegacyHashUpgrade(m_pendingUserId, m_pendingPassword, true);
            clearPendingAuthSecrets();
            return;
        }

        clearPendingAuthSecrets();
        completeAuthentication(m_pendingUserId, m_pendingUsername);
        return;
    }

    if (task == AuthTask::UpgradeHash)
    {
        const QString newHash = std::get<QString>(result);
        clearPendingAuthSecrets();

        if (newHash.isEmpty())
        {
            m_pendingGateAuthCompletion = false;
            sendMessage(MessageProtocol::createErrorMessage(500, "Password hash upgrade failed"));
            return;
        }

        m_authInFlight = m_pendingGateAuthCompletion;
        submitDbRequest(static_cast<int>(DatabaseWorker::Operation::UpdatePasswordHash),
                        QVariantMap{{"userId", m_pendingUserId},
                                    {"passwordHash", newHash}},
                        QVariantMap{{"stage", "upgrade_hash"}});
        return;
    }
}

void ClientConnection::completeAuthentication(int userId, const QString &username)
{
    setAuthenticated(userId, username);

    m_authTimer.stop();
    m_idleTimer.start(std::chrono::milliseconds(m_idleTimeoutMs), this);

    m_authInFlight = true;
    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::GetUserRooms),
                    QVariantMap{{"userId", userId}},
                    QVariantMap{{"stage", "auth_rooms"}});
}

void ClientConnection::onDbRequestFinished(quint64 requestId, int operation, const QVariantMap &result)
{
    auto it = m_pendingDb.find(requestId);
    if (it == m_pendingDb.end())
    {
        return;
    }

    const QVariantMap context = it.value();
    m_pendingDb.erase(it);

    const auto op = static_cast<DatabaseWorker::Operation>(operation);

    switch (op)
    {
    case DatabaseWorker::Operation::FindUserForAuth:
    {
        if (result.contains("error"))
        {
            m_authInFlight = false;
            sendMessage(MessageProtocol::createErrorMessage(500, "Database error"));
            return;
        }

        if (!result.value("found", false).toBool())
        {
            m_authInFlight = false;
            recordAuthFailure(m_pendingUsername, "Invalid credentials");
            return;
        }

        m_pendingUserId = result.value("userId").toInt();
        m_pendingUsername = result.value("username").toString();
        m_pendingStoredHash = result.value("passwordHash").toString();
        startPasswordVerification(m_pendingUsername, m_pendingPassword, m_pendingStoredHash, m_pendingUserId);
        return;
    }

    case DatabaseWorker::Operation::UpdatePasswordHash:
    {
        if (m_pendingGateAuthCompletion)
        {
            m_pendingGateAuthCompletion = false;
            completeAuthentication(m_pendingUserId, m_pendingUsername);
        }
        else if (!result.value("success", false).toBool())
        {
            qWarning() << "Background password hash upgrade failed";
        }
        return;
    }

    case DatabaseWorker::Operation::RegisterUser:
    {
        m_authInFlight = false;

        if (!result.value("success", false).toBool())
        {
            const QString errorMessage = result.value("errorMessage").toString();
            qWarning() << "Registration failed:" << errorMessage;
            sendMessage(MessageProtocol::createErrorMessage(400, errorMessage));
            return;
        }

        const QString username = result.value("username").toString();
        const int userId = result.value("userId").toInt();

        qInfo() << "User registered successfully:" << username;
        sendMessage(MessageProtocol::createRegisterSuccessMessage(userId, username));
        emit registrationSucceeded();
        return;
    }

    case DatabaseWorker::Operation::GetUserRooms:
    {
        m_authInFlight = false;

        if (!result.value("success", false).toBool())
        {
            sendMessage(MessageProtocol::createErrorMessage(500, "Database error"));
            return;
        }

        QStringList roomNames;
        const QVariantList roomVariants = result.value("rooms").toList();
        for (const QVariant &roomValue : roomVariants)
        {
            const QVariantMap room = roomValue.toMap();
            roomNames.append(room.value("name").toString());

            if (m_roomManager)
            {
                m_roomManager->addClientToRoom(QString::number(room.value("id").toInt()), this);
            }
        }

        sendMessage(MessageProtocol::createAuthSuccessMessage(m_userId.toInt(), m_username, roomNames));
        emit authenticated(this);
        qInfo() << "User authenticated:" << m_username << "with rooms:" << roomNames;
        return;
    }

    case DatabaseWorker::Operation::SaveRoomMessage:
    {
        if (!result.value("success", false).toBool())
        {
            sendMessage(MessageProtocol::createErrorMessage(500, "Failed to save message"));
            return;
        }

        const int messageId = result.value("messageId").toInt();
        const int roomId = context.value("roomId").toInt();
        const QString content = context.value("content").toString();
        const QDateTime createdAt = QDateTime::fromString(context.value("timestamp").toString(), Qt::ISODate);

        if (m_roomManager)
        {
            const QJsonObject broadcast = MessageProtocol::createChatMessage(messageId, roomId, m_username, content, createdAt);
            m_roomManager->broadcastToRoom(QString::number(roomId), broadcast, this);
        }

        qInfo() << "Message saved to database";
        emit roomMessageSent();
        return;
    }

    case DatabaseWorker::Operation::GetRoomMessages:
    {
        const int roomId = context.value("roomId").toInt();

        QJsonArray messagesArray;
        const QVariantList messages = result.value("messages").toList();
        for (const QVariant &value : messages)
        {
            const QVariantMap map = value.toMap();
            QJsonObject obj;
            obj["id"] = map.value("id").toInt();
            obj["roomId"] = map.value("roomId").toInt();
            obj["userId"] = map.value("userId").toInt();
            obj["content"] = map.value("content").toString();
            obj["timestamp"] = map.value("timestamp").toString();

            if (map.contains("editedAt"))
            {
                obj["editedAt"] = map.value("editedAt").toString();
            }
            if (map.contains("deletedAt"))
            {
                obj["deletedAt"] = map.value("deletedAt").toString();
            }

            messagesArray.append(obj);
        }

        sendMessage(MessageProtocol::createHistoryResponseMessage(roomId, messagesArray));
        return;
    }

    case DatabaseWorker::Operation::JoinRoom:
    {
        if (!result.value("success", false).toBool())
        {
            sendMessage(MessageProtocol::createErrorMessage(500, "Failed to join room"));
            return;
        }

        const int roomId = context.value("roomId").toInt();
        if (m_roomManager)
        {
            m_roomManager->addClientToRoom(QString::number(roomId), this);
        }
        sendMessage(MessageProtocol::createRoomJoinedMessage(roomId, "Room " + QString::number(roomId)));
        return;
    }

    case DatabaseWorker::Operation::LeaveRoom:
    {
        if (!result.value("success", false).toBool())
        {
            sendMessage(MessageProtocol::createErrorMessage(500, "Failed to leave room"));
            return;
        }

        const int roomId = context.value("roomId").toInt();
        if (m_roomManager)
        {
            m_roomManager->removeClientFromRoom(QString::number(roomId), this);
        }
        sendMessage(MessageProtocol::createRoomLeftMessage(roomId));
        return;
    }

    default:
        qWarning() << "ClientConnection received unexpected DB operation result:" << operation;
        return;
    }
}

void ClientConnection::handleChatMessage(const QJsonObject &message)
{
    const int roomId = MessageProtocol::getRoomId(message);
    const QString content = MessageProtocol::getContent(message);


    if (content.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Message content cannot be empty"));
        return;
    }

    if (!m_roomManager || !m_roomManager->isClientInRoom(QString::number(roomId), this))
    {
        sendMessage(MessageProtocol::createErrorMessage(403, "Not a member of this room"));
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVariantMap params;
    params["roomId"] = roomId;
    params["userId"] = m_userId.toInt();
    params["content"] = content;
    params["timestamp"] = now.toString(Qt::ISODate);

    QVariantMap context;
    context["roomId"] = roomId;
    context["content"] = content;
    context["timestamp"] = now.toString(Qt::ISODate);

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::SaveRoomMessage), params, context);
}

void ClientConnection::handlePrivateMessage(const QJsonObject &message)
{
    const QString recipient = MessageProtocol::getRecipient(message);
    const QString content = MessageProtocol::getContent(message);

    if (recipient.isEmpty() || content.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Recipient and content required"));
        return;
    }

    if (recipient == m_username)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Cannot message yourself"));
        return;
    }

    emit privateMessageRequested(this, recipient, content);
}

void ClientConnection::handleConversationRequest(const QJsonObject &message)
{
    const QString partner = MessageProtocol::getConversationPartner(message);
    const int limit = MessageProtocol::getLimit(message);
    const int beforeMessageId = MessageProtocol::getBeforeMessageId(message);

    if (partner.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Partner required"));
        return;
    }

    emit conversationRequested(this, partner, limit, beforeMessageId);
}

void ClientConnection::handleJoinRoom(const QJsonObject &message)
{
    const int roomId = MessageProtocol::getRoomId(message);

    if (roomId <= 0)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Invalid room id"));
        return;
    }

    QVariantMap params;
    params["roomId"] = roomId;
    params["userId"] = m_userId.toInt();

    QVariantMap context;
    context["roomId"] = roomId;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::JoinRoom), params, context);
}

void ClientConnection::handleLeaveRoom(const QJsonObject &message)
{
    const int roomId = MessageProtocol::getRoomId(message);

    if (roomId <= 0)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Invalid room id"));
        return;
    }

    QVariantMap params;
    params["roomId"] = roomId;
    params["userId"] = m_userId.toInt();

    QVariantMap context;
    context["roomId"] = roomId;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::LeaveRoom), params, context);
}

void ClientConnection::handleHistoryRequest(const QJsonObject &message)
{
    const int roomId = MessageProtocol::getRoomId(message);
    const int limit = MessageProtocol::getLimit(message);
    const int beforeMessageId = MessageProtocol::getBeforeMessageId(message);


    if (!m_roomManager || !m_roomManager->isClientInRoom(QString::number(roomId), this))
    {
        sendMessage(MessageProtocol::createErrorMessage(403, "Not a member of this room"));
        return;
    }

    QVariantMap params;
    params["roomId"] = roomId;
    params["limit"] = limit;
    params["beforeMessageId"] = beforeMessageId;

    QVariantMap context;
    context["roomId"] = roomId;

    submitDbRequest(static_cast<int>(DatabaseWorker::Operation::GetRoomMessages), params, context);
}

void ClientConnection::handlePresenceSubscribe(const QJsonObject &message)
{
    Q_UNUSED(message);
    emit presenceSubscribeRequested(this);
}

void ClientConnection::handleMarkRead(const QJsonObject &message)
{
    const QString with = MessageProtocol::getConversationPartner(message);
    const int beforeMessageId = MessageProtocol::getBeforeMessageId(message);

    if (with.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Partner required"));
        return;
    }

    emit markReadRequested(this, with, beforeMessageId);
}

void ClientConnection::handleUnreadRequest(const QJsonObject &message)
{
    Q_UNUSED(message);
    emit unreadSummaryRequested(this);
}

void ClientConnection::handleEditMessage(const QJsonObject &message)
{
    const int id = MessageProtocol::getEditMessageId(message);
    const QString content = MessageProtocol::getContent(message);

    if (id <= 0 || content.isEmpty())
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Message id and content required"));
        return;
    }
    emit editMessageRequested(this, id, content);
}

void ClientConnection::handleDeleteMessage(const QJsonObject &message)
{
    const int id = MessageProtocol::getEditMessageId(message);

    if (id <= 0)
    {
        sendMessage(MessageProtocol::createErrorMessage(400, "Message id required"));
        return;
    }
    emit deleteMessageRequested(this, id);
}

