#include "privatemessagerepository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QTimeZone>

PrivateMessageRepository::PrivateMessageRepository(QSqlDatabase db)
    :m_db{db}
{
}

bool PrivateMessageRepository::saveMessage(PrivateMessage &message)
{
    QSqlQuery query{m_db};
    query.prepare("INSERT INTO private_messages (sender_id, recipient_id, content, created_at) " "VALUES (?, ?, ?, ?)");
    query.addBindValue(message.senderId());
    query.addBindValue(message.recipientId());
    query.addBindValue(message.content());
    query.addBindValue(message.createdAt().toUTC().toString(Qt::ISODate));

    if (!query.exec())
    {
        qWarning() << "Failed to save private message:" << query.lastError().text();
        return false;
    }

    message.setId(query.lastInsertId().toInt());
    return true;
}

QList<PrivateMessage> PrivateMessageRepository::getConversation(int userId, int otherUserId, int limit, int beforeMessageId)
{
    QList<PrivateMessage>privateMessages;
    QSqlQuery query{m_db};

    if(beforeMessageId > 0)
    {
        query.prepare(
            "SELECT id, sender_id, recipient_id, content, created_at "
            "FROM private_messages "
            "WHERE ((sender_id = ? AND recipient_id = ?) "
            "    OR (sender_id = ? AND recipient_id = ?)) "
            "  AND id < ? "
            "ORDER BY id DESC LIMIT ?"
            );
        query.addBindValue(userId);
        query.addBindValue(otherUserId);
        query.addBindValue(otherUserId);
        query.addBindValue(userId);
        query.addBindValue(beforeMessageId);
        query.addBindValue(limit);
    }
    else
    {
        query.prepare(
            "SELECT id, sender_id, recipient_id, content, created_at "
            "FROM private_messages "
            "WHERE (sender_id = ? AND recipient_id = ?) "
            "   OR (sender_id = ? AND recipient_id = ?) "
            "ORDER BY id DESC LIMIT ?"
            );
        query.addBindValue(userId);
        query.addBindValue(otherUserId);
        query.addBindValue(otherUserId);
        query.addBindValue(userId);
        query.addBindValue(limit);
    }

    if(!query.exec())
    {
        qWarning() << "Failed to get conversation: " << query.lastError().text();
        return privateMessages;
    }

    while(query.next())
    {
        PrivateMessage pm;
        pm.setId(query.value(0).toInt());
        pm.setSenderId(query.value(1).toInt());
        pm.setRecipientId(query.value(2).toInt());
        pm.setContent(query.value(3).toString());

        QVariant createdVar = query.value(4);
        QDateTime createdAt;

        if (createdVar.isValid())
        {
            // ISO parse, ISO is written on insert
            createdAt = QDateTime::fromString(createdVar.toString(), Qt::ISODate);

            if (!createdAt.isValid())
            {
                createdAt = createdVar.toDateTime();
            }

            if (createdAt.isValid())
            {
                QTimeZone zone = QTimeZone::utc();
                createdAt.setTimeZone(zone);
            }
        }

        if (!createdAt.isValid())
        {
            createdAt = QDateTime::currentDateTimeUtc();
        }

        pm.setCreatedAt(createdAt);
        privateMessages.append(pm);
    }

    return privateMessages;
}

int PrivateMessageRepository::markAsRead(int recipientId, int senderId, int beforeMessageId)
{
    QSqlQuery querry{m_db};


    if(beforeMessageId > 0)
    {
        querry.prepare(
            "SELECT MAX(id) FROM private_messages "
            "WHERE sender_id = ? AND recipient_id = ? "
            "AND read_at is NULL AND id <= ?"
            );

        querry.addBindValue(senderId);
        querry.addBindValue(recipientId);
        querry.addBindValue(beforeMessageId);
    }
    else
    {
        querry.prepare(
            "SELECT MAX(id) FROM private_messages "
            "WHERE sender_id = ? AND recipient_id = ? AND read_at IS NULL"
            );

        querry.addBindValue(senderId);
        querry.addBindValue(recipientId);
    }
    if(!querry.exec() || !querry.next())
    {
        qCritical() << "markAsRead: max querry failed: " << querry.lastError().text();
        return -1;
    }

    const auto mVar = querry.value(0);

    if(mVar.isNull())
    {
        return -1;
    }

    const int highestId = mVar.toInt();

    QSqlQuery updQuerry(m_db);
    if(beforeMessageId > 0)
    {
        updQuerry.prepare(
            "UPDATE private_messages SET read_at = CURRENT_TIMESTAMP "
            "WHERE sender_id = ? AND recipient_id = ? "
            "  AND read_at IS NULL AND id <= ?"
            );
        updQuerry.addBindValue(senderId);
        updQuerry.addBindValue(recipientId);
        updQuerry.addBindValue(beforeMessageId);
    }
    else
    {
        updQuerry.prepare(
            "UPDATE private_messages SET read_at = CURRENT_TIMESTAMP "
            "WHERE sender_id = ? AND recipient_id = ? AND read_at IS NULL"
            );
        updQuerry.addBindValue(senderId);
        updQuerry.addBindValue(recipientId);
    }

    if (!updQuerry.exec())
    {
        qWarning() << "markAsRead: update failed:" << updQuerry.lastError().text();
        return -1;
    }

    return highestId;

}

QList<IPrivateMessageRepository::UnreadSummary> PrivateMessageRepository::getUnreadSummary(int recipientId)
{
    QList<UnreadSummary> out;

    QSqlQuery querry(m_db);
    querry.prepare(
        "SELECT u.id, u.username, COUNT(pm.id), MAX(pm.created_at) "
        "FROM private_messages pm "
        "INNER JOIN users u ON u.id = pm.sender_id "
        "WHERE pm.recipient_id = ? AND pm.read_at IS NULL "
        "GROUP BY u.id, u.username "
        "ORDER BY MAX(pm.created_at) DESC"
        );
    querry.addBindValue(recipientId);

    if(!querry.exec())
    {
        qWarning() << "getUnreadSummary failed:" << querry.lastError().text();
        return out;
    }

    while(querry.next())
    {
        UnreadSummary summary;
        summary.senderId = querry.value(0).toInt();
        summary.senderName = querry.value(1).toString();
        summary.unreadCount = querry.value(2).toInt();
        summary.lastMessageAt = querry.value(3).toDateTime();
        out.append(summary);
    }
    return out;
}

