#include "messageprotocol.h"

#include <climits>
#include <cmath>

namespace {
constexpr int kMaxContentLength = 10000;
constexpr int kMaxNameLength = 64;

bool isStringField(const QJsonObject &data, const QString &key, int maxLength = -1)
{
    const QJsonValue value = data.value(key);
    if (!value.isString())
    {
        return false;
    }
    const QString text = value.toString();
    if (text.isEmpty())
    {
        return false;
    }
    return maxLength < 0 || text.size() <= maxLength;
}

bool isIntField(const QJsonObject &data, const QString &key, int minValue, int maxValue)
{
    const QJsonValue value = data.value(key);
    if (!value.isDouble())
    {
        return false;
    }
    const double number = value.toDouble();
    if (number < minValue || number > maxValue || std::floor(number) != number)
    {
        return false;
    }
    return true;
}

bool isOptionalIntField(const QJsonObject &data, const QString &key, int minValue, int maxValue)
{
    if (!data.contains(key))
    {
        return true;
    }
    return isIntField(data, key, minValue, maxValue);
}

bool isArrayField(const QJsonObject &data, const QString &key)
{
    return data.value(key).isArray();
}

bool isBoolField(const QJsonObject &data, const QString &key)
{
    return data.value(key).isBool();
}
} // namespace

QJsonObject MessageProtocol::createAuthMessage(const QString &username, const QString &password)
{
    QJsonObject message;
    message["type"] = "auth";

    QJsonObject data;
    data["username"] = username;
    data["password"] = password;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createAuthSuccessMessage(int userId, const QString &username, const QStringList &rooms)
{
    QJsonObject message;
    message["type"] = "auth_success";

    QJsonObject data;
    data["userId"] = userId;
    data["username"] = username;
    data["rooms"] = QJsonArray::fromStringList(rooms);

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createRegisterMessage(const QString &username, const QString &password)
{
    QJsonObject message;
    message["type"] = "register";

    QJsonObject data;
    data["username"] = username;
    data["password"] = password;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createRegisterSuccessMessage(int userId, const QString &username)
{
    QJsonObject message;
    message["type"] = "register_success";

    QJsonObject data;
    data["userId"] = userId;
    data["username"] = username;

    message["data"] = data;

    return message;
}


QJsonObject MessageProtocol::createPrivateMessage(const QString &from, const QString &to, const QString &content, const QDateTime &timestamp, int messageId)
{
    QJsonObject message;
    message["type"] = "private_message";

    QJsonObject data;
    data["from"] = from;
    data["to"] = to;
    data["content"] = content;
    data["timestamp"] = timestamp.toString(Qt::ISODate);
    data["id"] = messageId;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createPrivateMessageSent(const QString &to, int messageId, const QDateTime &timestamp)
{
    QJsonObject message;
    message["type"] = "private_message_sent";

    QJsonObject data;
    data["to"] = to;
    data["id"] = messageId;
    data["timestamp"] = timestamp.toString(Qt::ISODate);

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createConversationRequest(const QString &with, int limit, int beforeMessageId)
{
    QJsonObject message;
    message["type"] = "conversation_request";

    QJsonObject data;
    data["with"] = with;
    data["limit"] = limit;
    data["beforeMessageId"] = beforeMessageId;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createConversationResponse(const QString &with, const QJsonArray &messages)
{
    QJsonObject message;
    message["type"] = "conversation_response";

    QJsonObject data;
    data["with"] = with;
    data["messages"] = messages;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createErrorMessage(int code, const QString &message)
{
    QJsonObject m;
    m["type"] = "error";

    QJsonObject data;
    data["code"] = code;
    data["message"] = message;

    m["data"] = data;

    return m;
}

QJsonObject MessageProtocol::createChatMessage(int messageId, int roomId, const QString &sender, const QString &content, const QDateTime &timestamp)
{
    QJsonObject message;
    message["type"] = "message";

    QJsonObject data;
    data["id"] = messageId;
    data["roomId"] = roomId;
    //Original
    data["sender"] = sender;
    //Alias for sender, so i dont need to change the tests
    data["username"] = sender;
    data["content"] = content;
    data["timestamp"] = timestamp.toString(Qt::ISODate);

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createSendMessage(int roomId, const QString &content)
{
    QJsonObject message;
    message["type"] = "message";

    QJsonObject data;
    data["roomId"] = roomId;
    data["content"] = content;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createJoinRoomMessage(int roomId)
{
    QJsonObject message;
    message["type"] = "join_room";

    QJsonObject data;
    data["roomId"] = roomId;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createLeaveRoomMessage(int roomId)
{
    QJsonObject message;
    message["type"] = "leave_room";

    QJsonObject data;
    data["roomId"] = roomId;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createRoomJoinedMessage(int roomId, const QString &roomName)
{
    QJsonObject message;
    message["type"] = "room_joined";

    QJsonObject data;
    data["roomId"] = roomId;
    data["roomName"] = roomName;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createRoomLeftMessage(int roomId)
{
    QJsonObject message;
    message["type"] = "room_left";

    QJsonObject data;
    data["roomId"] = roomId;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createUserListMessage(const QStringList &users)
{
    QJsonObject message;
    message["type"] = "user_list";

    QJsonObject data;
    data["users"] = QJsonArray::fromStringList(users);

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createHistoryRequestMessage(int roomId, int limit, int beforeMessageId)
{
    QJsonObject message;
    message["type"] = "history_request";

    QJsonObject data;
    data["roomId"] = roomId;
    data["limit"] = limit;
    data["beforeMessageId"] = beforeMessageId;

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createHistoryResponseMessage(int roomId, const QJsonArray &messages)
{
    QJsonObject message;
    message["type"] = "history_response";

    QJsonObject data;
    data["roomId"] = roomId;
    data["messages"] = messages;

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createEditMessageMessage(int messageId, const QString &content)
{
    QJsonObject message;
    message["type"] = "edit_message";

    QJsonObject data;
    data["id"] = messageId;
    data["content"] = content;

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createDeleteMessageMessage(int messageId)
{
    QJsonObject message;
    message["type"] = "delete_message";

    QJsonObject data;
    data["id"] = messageId;

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createMessageEditedMessage(int messageId, const QString &content, const QDateTime &editedAt)
{
    QJsonObject message;
    message["type"] = "message_edited";

    QJsonObject data;
    data["id"] = messageId;
    data["content"] = content;
    data["editedAt"] = editedAt.toString(Qt::ISODate);

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createMessageDeletedMessage(int messageId)
{
    QJsonObject message;
    message["type"] = "message_deleted";

    QJsonObject data;
    data["id"] = messageId;

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createPresenceSubscribeMessage()
{
    QJsonObject message;
    message["type"] = "presence_subscribe";

    QJsonObject data;

    message["data"] = data;
    return message;
}

QJsonObject MessageProtocol::createPresenceUpdateMessage(const QString &userId, const QString &username, bool online, qint64 lastSeen)
{
    QJsonObject message;
    message["type"] = "presence_update";

    QJsonObject data;
    data["userId"] = userId;
    data["username"] = username;
    data["online"] = online;
    data["lastSeen"] = lastSeen;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createPresenceListMessage(const QJsonArray &users)
{
    QJsonObject message;
    message["type"] = "presence_list";

    QJsonObject data;
    data["users"] = users;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createMarkReadMessage(const QString &with, int beforeMessageId)
{
    QJsonObject message;
    message["type"] = "mark_read";

    QJsonObject data;
    data["with"] = with;
    data["beforeMessageId"] = beforeMessageId;

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createReadReceiptMessage(const QString &from, int beforeMessageId, const QDateTime &readAt)
{
    QJsonObject message;
    message["type"] = "read_receipt";

    QJsonObject data;
    data["from"] = from;
    data["beforeMessageId"] = beforeMessageId;
    data["readAt"] = readAt.toString(Qt::ISODate);

    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createUnreadRequestMessage()
{
    QJsonObject message;
    message["type"] = "unread_request";

    QJsonObject data;
    message["data"] = data;

    return message;
}

QJsonObject MessageProtocol::createUnreadSummaryMessage(const QJsonArray &conversations)
{
    QJsonObject message;
    message["type"] = "unread_summary";

    QJsonObject data;
    data["conversations"] = conversations;
    message["data"] = data;

    return message;
}

MessageProtocol::MessageType MessageProtocol::getMessageType(const QJsonObject &message)
{
    if(!message.contains("type"))
    {
        return MessageType::Unknown;
    }

    return stringToMessageType(message["type"].toString());
}

bool MessageProtocol::isValidMessage(const QJsonObject &message)
{
    if (message.isEmpty())
    {
        return false;
    }

    if (!message.contains("type") || !message["type"].isString())
    {
        return false;
    }

    const MessageType type = getMessageType(message);
    if (type == MessageType::Unknown)
    {
        return false;
    }

    if (!message.contains("data") || !message["data"].isObject())
    {
        return false;
    }

    const QJsonObject data = message["data"].toObject();

    switch (type)
    {
    case MessageType::Auth:
    case MessageType::Register:
        return isStringField(data, "username", 512) && isStringField(data, "password", 512);

    case MessageType::Message:
        return isIntField(data, "roomId", 1, INT_MAX) && isStringField(data, "content", kMaxContentLength);

    case MessageType::PrivateMessage:
        return isStringField(data, "to", kMaxNameLength) && isStringField(data, "content", kMaxContentLength);

    case MessageType::ConversationRequest:
        return isStringField(data, "with", kMaxNameLength) && isOptionalIntField(data, "limit", 1, 200) && isOptionalIntField(data, "beforeMessageId", -1, INT_MAX);

    case MessageType::JoinRoom:
    case MessageType::LeaveRoom:
        return isIntField(data, "roomId", 1, INT_MAX);

    case MessageType::HistoryRequest:
        return isIntField(data, "roomId", 1, INT_MAX) && isOptionalIntField(data, "limit", 1, 200) && isOptionalIntField(data, "beforeMessageId", -1, INT_MAX);

    case MessageType::PresenceSubscribe:
    case MessageType::UnreadRequest:
        return true;

    case MessageType::MarkRead:
        return isStringField(data, "with", kMaxNameLength) && isOptionalIntField(data, "beforeMessageId", -1, INT_MAX);

    case MessageType::EditMessage:
        return isIntField(data, "id", 1, INT_MAX) && isStringField(data, "content", kMaxContentLength);

    case MessageType::DeleteMessage:
        return isIntField(data, "id", 1, INT_MAX);

    case MessageType::AuthSuccess:
        return isIntField(data, "userId", 1, INT_MAX) && isStringField(data, "username", kMaxNameLength) && isArrayField(data, "rooms");

    case MessageType::RegisterSuccess:
        return isIntField(data, "userId", 1, INT_MAX) && isStringField(data, "username", kMaxNameLength);

    case MessageType::PrivateMessageSent:
        return isStringField(data, "to", kMaxNameLength) && isIntField(data, "id", 1, INT_MAX) && isStringField(data, "timestamp", 128);

    case MessageType::ConversationResponse:
        return isStringField(data, "with", kMaxNameLength) && isArrayField(data, "messages");

    case MessageType::RoomJoined:
        return isIntField(data, "roomId", 1, INT_MAX) && isStringField(data, "roomName", 128);

    case MessageType::RoomLeft:
        return isIntField(data, "roomId", 1, INT_MAX);

    case MessageType::UserList:
        return isArrayField(data, "users");

    case MessageType::HistoryResponse:
        return isIntField(data, "roomId", 1, INT_MAX) && isArrayField(data, "messages");

    case MessageType::PresenceUpdate:
        return isStringField(data, "userId", kMaxNameLength) && isStringField(data, "username", kMaxNameLength) && isBoolField(data, "online") && isIntField(data, "lastSeen", 0, INT_MAX);

    case MessageType::PresenceList:
        return isArrayField(data, "users");

    case MessageType::ReadReceipt:
        return isStringField(data, "from", kMaxNameLength)&& isIntField(data, "beforeMessageId", -1, INT_MAX) && isStringField(data, "readAt", 128);

    case MessageType::UnreadSummary:
        return isArrayField(data, "conversations");

    case MessageType::MessageEdited:
        return isIntField(data, "id", 1, INT_MAX) && isStringField(data, "content", kMaxContentLength) && isStringField(data, "editedAt", 128);

    case MessageType::MessageDeleted:
        return isIntField(data, "id", 1, INT_MAX);

    case MessageType::Error:
        return isIntField(data, "code", 100, 599) && isStringField(data, "message", 512);

    // AuthError currently has no server-side creator, but keep the parser strict.
    case MessageType::AuthError:
        return isIntField(data, "code", 100, 599) && isStringField(data, "message", 512);

    default:
        return false;
    }
}

QString MessageProtocol::getUsername(const QJsonObject &message)
{
    return message["data"].toObject()["username"].toString();
}

QString MessageProtocol::getPassword(const QJsonObject &message)
{
    return message["data"].toObject()["password"].toString();
}

QString MessageProtocol::getContent(const QJsonObject &message)
{
    return message["data"].toObject()["content"].toString();
}

QString MessageProtocol::getRecipient(const QJsonObject &message)
{
    return message["data"].toObject()["to"].toString();
}

QString MessageProtocol::getConversationPartner(const QJsonObject &message)
{
    return message["data"].toObject()["with"].toString();
}

int MessageProtocol::getRoomId(const QJsonObject &message)
{
    return message["data"].toObject()["roomId"].toInt();
}

int MessageProtocol::getUserId(const QJsonObject &message)
{
    return message["data"].toObject()["userId"].toInt();
}

int MessageProtocol::getMessageId(const QJsonObject &message)
{
    return message["data"].toObject()["id"].toInt();
}

int MessageProtocol::getLimit(const QJsonObject &message)
{
    return qBound(1, message["data"].toObject()["limit"].toInt(50), 200);
}

int MessageProtocol::getBeforeMessageId(const QJsonObject &message)
{
    return qMax(-1, message["data"].toObject()["beforeMessageId"].toInt(-1));
}

QString MessageProtocol::getSender(const QJsonObject &message)
{
    return message["data"].toObject()["sender"].toString();
}

QString MessageProtocol::getSenderName(const QJsonObject &message)
{
    return message["data"].toObject()["from"].toString();
}

QDateTime MessageProtocol::getTimestamp(const QJsonObject &message)
{
    QString timestamp = message["data"].toObject()["timestamp"].toString();
    return QDateTime::fromString(timestamp, Qt::ISODate);
}

QStringList MessageProtocol::getRooms(const QJsonObject &message)
{
    QJsonArray roomArr = message["data"].toObject()["rooms"].toArray();
    QStringList rooms;

    for(const auto &val : std::as_const(roomArr))
    {
        rooms.append(val.toString());
    }

    return rooms;
}

QStringList MessageProtocol::getUsers(const QJsonObject &message)
{
    QJsonArray usersArr = message["data"].toObject()["users"].toArray();
    QStringList users;

    for(const auto &val : std::as_const(usersArr))
    {
        users.append(val.toString());
    }

    return users;
}

QJsonArray MessageProtocol::getMessages(const QJsonObject &message)
{
    return message["data"].toObject()["messages"].toArray();
}

QJsonArray MessageProtocol::getUsersArray(const QJsonObject &message)
{
    return message["data"].toObject()["users"].toArray();
}

int MessageProtocol::getEditMessageId(const QJsonObject &message)
{
    return message["data"].toObject()["id"].toInt(-1);
}

QString MessageProtocol::messageTypeToString(MessageType type)
{
    switch(type)
    {
    case MessageType::Auth:
    {
        return "auth";
    }
    case MessageType::Message:
    {
        return "message";
    }
    case MessageType::PrivateMessage:
    {
        return "private_message";
    }
    case MessageType::PrivateMessageSent:
    {
        return "private_message_sent";
    }
    case MessageType::ConversationRequest:
    {
        return "conversation_request";
    }
    case MessageType::ConversationResponse:
    {
        return "conversation_response";
    }
    case MessageType::AuthSuccess:
    {
        return "auth_success";
    }
    case MessageType::AuthError:
    {
        return "auth_error";
    }
    case MessageType::Register:
    {
        return "register";
    }
    case MessageType::RegisterSuccess:
    {
        return "register_success";
    }
    case MessageType::Error:
    {
        return "error";
    }
    case MessageType::JoinRoom:
    {
        return "join_room";
    }
    case MessageType::LeaveRoom:
    {
        return "leave_room";
    }
    case MessageType::RoomJoined:
    {
        return "room_joined";
    }
    case MessageType::RoomLeft:
    {
        return "room_left";
    }
    case MessageType::UserList:
    {
        return "user_list";
    }
    case MessageType::HistoryRequest:
    {
        return "history_request";
    }
    case MessageType::HistoryResponse:
    {
        return "history_response";
    }
    case MessageType::PresenceSubscribe:
    {
        return "presence_subscribe";
    }
    case MessageType::PresenceUpdate:
    {
        return "presence_update";
    }
    case MessageType::PresenceList:
    {
        return "presence_list";
    }
    case MessageType::MarkRead:
    {
        return "mark_read";
    }
    case MessageType::ReadReceipt:
    {
        return "read_receipt";
    }
    case MessageType::UnreadRequest:
    {
        return "unread_request";
    }
    case MessageType::UnreadSummary:
    {
        return "unread_summary";
    }
    case MessageType::EditMessage:
    {
        return "edit_message";
    }
    case MessageType::DeleteMessage:
    {
        return "delete_message";
    }
    case MessageType::MessageEdited:
    {
        return "message_edited";
    }
    case MessageType::MessageDeleted:
    {
        return "message_deleted";
    }
    default:
        return "unknown";
    }
}

MessageProtocol::MessageType MessageProtocol::stringToMessageType(const QString &type)
{
    if(type == "auth")
    {
        return MessageType::Auth;
    }
    if(type == "message")
    {
        return MessageType::Message;
    }
    if(type == "private_message")
    {
        return MessageType::PrivateMessage;
    }
    if(type == "private_message_sent")
    {
        return MessageType::PrivateMessageSent;
    }
    if(type == "conversation_request")
    {
        return MessageType::ConversationRequest;
    }
    if(type == "conversation_response")
    {
        return MessageType::ConversationResponse;
    }
    if(type == "auth_success")
    {
        return MessageType::AuthSuccess;
    }
    if(type == "auth_error")
    {
        return MessageType::AuthError;
    }
    if(type == "register")
    {
        return MessageType::Register;
    }
    if(type == "register_success")
    {
        return MessageType::RegisterSuccess;
    }
    if(type == "error")
    {
        return MessageType::Error;
    }
    if(type == "join_room")
    {
        return MessageType::JoinRoom;
    }
    if(type == "leave_room")
    {
        return MessageType::LeaveRoom;
    }
    if(type == "room_joined")
    {
        return MessageType::RoomJoined;
    }
    if(type == "room_left")
    {
        return MessageType::RoomLeft;
    }
    if(type == "user_list")
    {
        return MessageType::UserList;
    }
    if(type == "history_request")
    {
        return MessageType::HistoryRequest;
    }
    if(type == "history_response")
    {
        return MessageType::HistoryResponse;
    }
    if(type == "presence_subscribe")
    {
        return MessageType::PresenceSubscribe;
    }
    if(type == "presence_update")
    {
        return MessageType::PresenceUpdate;
    }
    if(type == "presence_list")
    {
        return MessageType::PresenceList;
    }
    if(type == "mark_read")
    {
        return MessageType::MarkRead;
    }
    if(type == "read_receipt")
    {
        return MessageType::ReadReceipt;
    }
    if(type == "unread_request")
    {
        return MessageType::UnreadRequest;
    }
    if(type == "unread_summary")
    {
        return MessageType::UnreadSummary;
    }
    if(type == "edit_message")
    {
        return MessageType::EditMessage;
    }
    if(type == "delete_message")
    {
        return MessageType::DeleteMessage;
    }
    if(type == "message_edited")
    {
        return MessageType::MessageEdited;
    }
    if(type == "message_deleted")
    {
        return MessageType::MessageDeleted;
    }
    return MessageType::Unknown;
}
