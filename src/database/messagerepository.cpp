#include "MessageRepository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QTimeZone>
#include <QVariant>
#include "SqlDateHelper.h"



MessageRepository::MessageRepository(QSqlDatabase db)
    : m_db{db}
{
}

bool MessageRepository::saveMessage(Message &message)
{
    QSqlQuery query{m_db};
    query.prepare("INSERT INTO messages (room_id, user_id, content, created_at) VALUES (?, ?, ?, ?)");

    query.addBindValue(message.roomId());
    query.addBindValue(message.userId());
    query.addBindValue(message.content());
    query.addBindValue(message.createdAt().toUTC().toString(Qt::ISODate));


    if (!query.exec())
    {
        qWarning() << "Failed to save message:" << query.lastError().text();
        return false;
    }

    message.setId(query.lastInsertId().toInt());

    return true;
}

QList<Message> MessageRepository::getRoomMessages(int roomId, int limit, int beforeMessageId)
{
    QList<Message> messages;
    QSqlQuery query{m_db};

    if (beforeMessageId > 0)
    {
        query.prepare(
            "SELECT id, room_id, user_id, content, created_at, edited_at, deleted_at "
            "FROM messages "
            "WHERE room_id = ? AND id < ? "
            "ORDER BY id DESC LIMIT ?"
            );
        query.addBindValue(roomId);
        query.addBindValue(beforeMessageId);
        query.addBindValue(limit);
    }
    else
    {
        query.prepare(
            "SELECT id, room_id, user_id, content, created_at, edited_at, deleted_at "
            "FROM messages "
            "WHERE room_id = ? "
            "ORDER BY id DESC LIMIT ?"
            );
        query.addBindValue(roomId);
        query.addBindValue(limit);
    }

    if (!query.exec())
    {
        qWarning() << "Failed to get messages:" << query.lastError().text();
        return messages;
    }

    while (query.next())
    {
        Message message;
        message.setId(query.value(0).toInt());
        message.setRoomId(query.value(1).toInt());
        message.setUserId(query.value(2).toInt());
        message.setContent(query.value(3).toString());

        message.setCreatedAt(SqlDateHelper::parseOrNow(query.value(4)));
        message.setEditedAt(SqlDateHelper::parseNullable(query.value(5)));
        message.setDeletedAt(SqlDateHelper::parseNullable(query.value(6)));

        messages.append(message);
    }

    return messages;
}
bool MessageRepository::softDeleteMessage(int messageId)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET deleted_at = ? " "WHERE id = ? AND deleted_at IS NULL");
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(messageId);

    if (!query.exec())
    {
        qWarning() << "softDeleteMessage failed: " << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool MessageRepository::updateMessage(int messageId, const QString &newContent)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET content = ?, edited_at = ? WHERE id = ? AND deleted_at IS NULL");
    query.addBindValue(newContent);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.addBindValue(messageId);

    if (!query.exec())
    {
        qWarning() << "Failed to update message:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

IMessageRepository::MessageOwnership MessageRepository::lookupMessage(int messageId)
{
    MessageOwnership result;
    QSqlQuery query(m_db);
    query.prepare("SELECT user_id, room_id, deleted_at IS NOT NULL " "FROM messages WHERE id = ?");
    query.addBindValue(messageId);
    if (!query.exec() || !query.next())
    {
        return result;
    }
    result.found  = true;
    result.userId = query.value(0).toInt();
    result.roomId = query.value(1).toInt();
    result.deleted = query.value(2).toBool();
    return result;
}
