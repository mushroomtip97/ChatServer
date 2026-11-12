#include "databaseworker.h"

#include "authmanager.h"
#include "IAuthenticator.h"
#include "messagerepository.h"
#include "privatemessagerepository.h"
#include "IMessageRepository.h"
#include "IPrivateMessageRepository.h"
#include "transactionguard.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QChronoTimer>
#include <chrono>
#include <QVariantList>

namespace {

QString iso(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODate);
}

QVariantMap roomMessageToMap(const Message &msg)
{
    QVariantMap map;
    map["id"] = msg.id();
    map["roomId"] = msg.roomId();
    map["userId"] = msg.userId();
    map["content"] = msg.content();
    map["timestamp"] = iso(msg.createdAt());
    if (msg.editedAt().isValid())
    {
        map["editedAt"] = iso(msg.editedAt());
    }
    if (msg.deletedAt().isValid())
    {
        map["deletedAt"] = iso(msg.deletedAt());
    }
    return map;
}

QVariantMap privateMessageToMap(const PrivateMessage &pm)
{
    QVariantMap map;
    map["id"] = pm.id();
    map["senderId"] = pm.senderId();
    map["recipientId"] = pm.recipientId();
    map["content"] = pm.content();
    map["timestamp"] = iso(pm.createdAt());
    return map;
}

QVariantMap unreadToMap(const IPrivateMessageRepository::UnreadSummary &summary)
{
    QVariantMap map;
    map["senderName"] = summary.senderName;
    map["count"] = summary.unreadCount;
    map["lastMessageAt"] = iso(summary.lastMessageAt);
    return map;
}

} // namespace

DatabaseWorker::DatabaseWorker(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVariantMap>("QVariantMap");
}

quint64 DatabaseWorker::nextRequestId()
{
    static std::atomic<quint64> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

bool DatabaseWorker::initialize(const QString &dbPath, bool seedDemoUser)
{
    if (!m_dbManager.initialize(dbPath, seedDemoUser))
    {
        m_healthy.store(false);
        return false;
    }

    m_healthy.store(true);

    m_auth = std::make_unique<AuthManager>(m_dbManager.database());
    m_messages = std::make_unique<MessageRepository>(m_dbManager.database());
    m_privateMessages = std::make_unique<PrivateMessageRepository>(m_dbManager.database());

    m_healthTimer = new QChronoTimer(this);
    m_healthTimer->setInterval(std::chrono::seconds(15));
    m_healthTimer->setTimerType(Qt::CoarseTimer);
    connect(m_healthTimer, &QChronoTimer::timeout, this, [this]()
    {
        if (!m_dbManager.database().isOpen())
        {
            m_healthy.store(false);
            return;
        }

        QSqlQuery query(m_dbManager.database());
        m_healthy.store(query.exec("SELECT 1") && query.next());
    });
    m_healthTimer->start();

    return true;
}

void DatabaseWorker::close()
{
    m_healthy.store(false);
    if (m_healthTimer)
    {
        m_healthTimer->stop();
    }
    m_auth.reset();
    m_messages.reset();
    m_privateMessages.reset();
    m_dbManager.close();
}

void DatabaseWorker::markHealthy(bool healthy)
{
    m_healthy.store(healthy);
}

void DatabaseWorker::submitRequest(quint64 requestId, int operation, const QVariantMap &params)
{
    if (!m_dbManager.database().isOpen())
    {
        QVariantMap result;
        result["success"] = false;
        result["error"] = "Database is not available";
        emit requestFinished(requestId, operation, result);
        return;
    }

    QVariantMap result = execute(static_cast<Operation>(operation), params);

    // Expected "user not found" results have success=false but no error key;
    // only real SQL failures should mark the database unhealthy.
    markHealthy(!result.contains("error"));

    emit requestFinished(requestId, operation, result);
}

QVariantMap DatabaseWorker::execute(DatabaseWorker::Operation operation, const QVariantMap &params)
{
    QVariantMap result;
    const QSqlDatabase db = m_dbManager.database();

    switch (operation)
    {
    case Operation::FindUserForAuth:
    {
        QSqlQuery query(db);
        query.prepare("SELECT id, username, password_hash FROM users WHERE username = ?");
        query.addBindValue(params.value("username").toString());

        if (!query.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }

        if (!query.next())
        {
            result["success"] = true;
            result["found"] = false;
            return result;
        }

        result["success"] = true;
        result["found"] = true;
        result["userId"] = query.value(0).toInt();
        result["username"] = query.value(1).toString();
        result["passwordHash"] = query.value(2).toString();
        return result;
    }

    case Operation::RegisterUser:
    {
        const AuthResult authResult = m_auth->registerUserWithHash(params.value("username").toString(), params.value("passwordHash").toString());
        result["success"] = authResult.success;
        result["userId"] = authResult.userId;
        result["username"] = authResult.username;
        result["errorMessage"] = authResult.errorMessage;
        return result;
    }

    case Operation::UpdatePasswordHash:
    {
        const bool ok = m_auth->updatePasswordHash(params.value("userId").toInt(), params.value("passwordHash").toString());
        result["success"] = ok;
        if (!ok)
        {
            result["error"] = "Failed to update password hash";
        }
        return result;
    }

    case Operation::GetUserRooms:
    {
        QSqlQuery query(db);
        query.prepare(
            "SELECT r.id, r.name FROM rooms r "
            "INNER JOIN room_members rm ON r.id = rm.room_id "
            "WHERE rm.user_id = ?");
        query.addBindValue(params.value("userId").toInt());

        if (!query.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }

        QVariantList rooms;
        while (query.next())
        {
            QVariantMap room;
            room["id"] = query.value(0).toInt();
            room["name"] = query.value(1).toString();
            rooms.append(room);
        }

        result["success"] = true;
        result["rooms"] = rooms;
        return result;
    }

    case Operation::SaveRoomMessage:
    {
        Message msg;
        msg.setRoomId(params.value("roomId").toInt());
        msg.setUserId(params.value("userId").toInt());
        msg.setContent(params.value("content").toString());
        msg.setCreatedAt(QDateTime::fromString(params.value("timestamp").toString(), Qt::ISODate));

        if (!m_messages->saveMessage(msg))
        {
            result["success"] = false;
            result["error"] = "Failed to save message";
            return result;
        }

        result["success"] = true;
        result["messageId"] = msg.id();
        return result;
    }

    case Operation::GetRoomMessages:
    {
        const int limit = qBound(1, params.value("limit").toInt(), 200);
        const int beforeMessageId = qMax(-1, params.value("beforeMessageId").toInt());

        const QList<Message> messages = m_messages->getRoomMessages(params.value("roomId").toInt(), limit, beforeMessageId);

        QVariantList list;
        for (const Message &msg : messages)
        {
            list.append(roomMessageToMap(msg));
        }

        result["success"] = true;
        result["messages"] = list;
        return result;
    }

    case Operation::UpdateMessage:
    {
        const auto info = m_messages->lookupMessage(params.value("messageId").toInt());

        if (!info.found)
        {
            result["success"] = false;
            result["notFound"] = true;
            return result;
        }
        if (info.userId != params.value("userId").toInt())
        {
            result["success"] = false;
            result["forbidden"] = true;
            return result;
        }
        if (info.deleted)
        {
            result["success"] = false;
            result["deleted"] = true;
            return result;
        }

        if (!m_messages->updateMessage(params.value("messageId").toInt(), params.value("content").toString()))
        {
            result["success"] = false;
            result["error"] = "Failed to update message";
            return result;
        }

        result["success"] = true;
        result["roomId"] = info.roomId;
        return result;
    }

    case Operation::DeleteMessage:
    {
        const auto info = m_messages->lookupMessage(params.value("messageId").toInt());

        if (!info.found)
        {
            result["success"] = false;
            result["notFound"] = true;
            return result;
        }
        if (info.userId != params.value("userId").toInt())
        {
            result["success"] = false;
            result["forbidden"] = true;
            return result;
        }
        if (info.deleted)
        {
            result["success"] = true;
            result["alreadyDeleted"] = true;
            result["roomId"] = info.roomId;
            return result;
        }

        if (!m_messages->softDeleteMessage(params.value("messageId").toInt()))
        {
            result["success"] = false;
            result["error"] = "Failed to delete message";
            return result;
        }

        result["success"] = true;
        result["roomId"] = info.roomId;
        return result;
    }

    case Operation::JoinRoom:
    {
        const int roomId = params.value("roomId").toInt();
        const int userId = params.value("userId").toInt();

        TransactionGuard tg(db);
        if (!tg.active())
        {
            result["success"] = false;
            result["error"] = "Failed to start transaction";
            return result;
        }

        QSqlQuery check(db);
        check.prepare("SELECT id FROM rooms WHERE id = ?");
        check.addBindValue(roomId);

        if (!check.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }

        if (!check.next())
        {
            QSqlQuery create(db);
            create.prepare("INSERT OR IGNORE INTO rooms (id, name) VALUES (?, ?)");
            create.addBindValue(roomId);
            create.addBindValue(QString("room_%1").arg(roomId));
            if (!create.exec())
            {
                result["success"] = false;
                result["error"] = "Failed to create room";
                return result;
            }
        }

        QSqlQuery membership(db);
        membership.prepare("INSERT OR IGNORE INTO room_members (room_id, user_id) VALUES (?, ?)");
        membership.addBindValue(roomId);
        membership.addBindValue(userId);
        if (!membership.exec())
        {
            result["success"] = false;
            result["error"] = "Failed to join room";
            return result;
        }

        if (!tg.commit())
        {
            result["success"] = false;
            result["error"] = "Failed to commit transaction";
            return result;
        }

        result["success"] = true;
        result["roomId"] = roomId;
        return result;
    }

    case Operation::LeaveRoom:
    {
        QSqlQuery query(db);
        query.prepare("DELETE FROM room_members WHERE room_id = ? AND user_id = ?");
        query.addBindValue(params.value("roomId").toInt());
        query.addBindValue(params.value("userId").toInt());

        if (!query.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }

        result["success"] = true;
        result["roomId"] = params.value("roomId").toInt();
        return result;
    }

    case Operation::SendPrivateMessage:
    {
        QSqlQuery recipientQuery(db);
        recipientQuery.prepare("SELECT id, username FROM users WHERE username = ?");
        recipientQuery.addBindValue(params.value("recipientName").toString());

        if (!recipientQuery.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }
        if (!recipientQuery.next())
        {
            result["success"] = false;
            result["notFound"] = true;
            return result;
        }

        const int recipientId = recipientQuery.value(0).toInt();
        const QString canonicalRecipient = recipientQuery.value(1).toString();

        PrivateMessage pm;
        pm.setSenderId(params.value("senderId").toInt());
        pm.setRecipientId(recipientId);
        pm.setContent(params.value("content").toString());
        pm.setCreatedAt(QDateTime::fromString(params.value("timestamp").toString(), Qt::ISODate));

        if (!m_privateMessages->saveMessage(pm))
        {
            result["success"] = false;
            result["error"] = "Failed to save private message";
            return result;
        }

        result["success"] = true;
        result["recipientId"] = recipientId;
        result["recipientName"] = canonicalRecipient;
        result["messageId"] = pm.id();
        result["timestamp"] = iso(pm.createdAt());
        return result;
    }

    case Operation::GetConversation:
    {
        QSqlQuery partnerQuery(db);
        partnerQuery.prepare("SELECT id, username FROM users WHERE username = ?");
        partnerQuery.addBindValue(params.value("partnerName").toString());

        if (!partnerQuery.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }
        if (!partnerQuery.next())
        {
            result["success"] = false;
            result["notFound"] = true;
            return result;
        }

        const int partnerId = partnerQuery.value(0).toInt();
        const QString canonicalPartner = partnerQuery.value(1).toString();
        const int requesterId = params.value("requesterId").toInt();
        const int limit = qBound(1, params.value("limit").toInt(), 200);
        const int beforeMessageId = qMax(-1, params.value("beforeMessageId").toInt());

        const QList<PrivateMessage> messages = m_privateMessages->getConversation(requesterId, partnerId, limit, beforeMessageId);

        QVariantList list;
        for (const PrivateMessage &pm : messages)
        {
            list.append(privateMessageToMap(pm));
        }

        result["success"] = true;
        result["partnerName"] = canonicalPartner;
        result["messages"] = list;
        return result;
    }

    case Operation::MarkRead:
    {
        QSqlQuery userQuery(db);
        userQuery.prepare("SELECT id, username FROM users WHERE username = ?");
        userQuery.addBindValue(params.value("withUsername").toString());

        if (!userQuery.exec())
        {
            result["success"] = false;
            result["error"] = "Database error";
            return result;
        }
        if (!userQuery.next())
        {
            result["success"] = false;
            result["notFound"] = true;
            return result;
        }

        const int otherId = userQuery.value(0).toInt();
        const int recipientId = params.value("recipientId").toInt();
        const int beforeMessageId = qMax(-1, params.value("beforeMessageId").toInt());

        const int highestRead = m_privateMessages->markAsRead(recipientId, otherId, beforeMessageId);

        result["success"] = true;
        result["otherId"] = otherId;
        result["highestRead"] = highestRead;
        return result;
    }

    case Operation::GetUnreadSummary:
    {
        const auto summaries = m_privateMessages->getUnreadSummary(params.value("recipientId").toInt());

        QVariantList list;
        for (const auto &summary : summaries)
        {
            list.append(unreadToMap(summary));
        }

        result["success"] = true;
        result["conversations"] = list;
        return result;
    }

    case Operation::GetUsernameById:
    {
        QSqlQuery query(db);
        query.prepare("SELECT username FROM users WHERE id = ?");
        query.addBindValue(params.value("userId").toInt());

        if (!query.exec() || !query.next())
        {
            result["success"] = false;
            result["error"] = "User not found";
            return result;
        }

        result["success"] = true;
        result["username"] = query.value(0).toString();
        return result;
    }
    }

    result["success"] = false;
    result["error"] = "Unknown database operation";
    return result;
}
