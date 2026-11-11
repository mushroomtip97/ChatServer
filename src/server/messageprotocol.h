#ifndef MESSAGEPROTOCOL_H
#define MESSAGEPROTOCOL_H

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QDateTime>

class MessageProtocol
{
public:

    enum class MessageType
    {
        Unknown,
        Auth,
        Message,
        PrivateMessage,
        PrivateMessageSent,
        ConversationRequest,
        ConversationResponse,
        AuthSuccess,
        AuthError,
        Register,
        RegisterSuccess,
        Error,
        JoinRoom,
        LeaveRoom,
        RoomJoined,
        RoomLeft,
        UserList,
        HistoryRequest,
        HistoryResponse,
        PresenceSubscribe,
        PresenceUpdate,
        PresenceList,
        MarkRead,
        ReadReceipt,
        UnreadRequest,
        UnreadSummary,
        EditMessage,
        DeleteMessage,
        MessageEdited,
        MessageDeleted,
    };

    //Authentication
    static QJsonObject createAuthMessage(const QString &username, const QString &password);
    static QJsonObject createAuthSuccessMessage(int userId, const QString &username, const QStringList &rooms);
    static QJsonObject createRegisterMessage(const QString &username, const QString &password);
    static QJsonObject createRegisterSuccessMessage(int userId, const QString &username);

    //Private messaging
    static QJsonObject createPrivateMessage(const QString &from, const QString &to, const QString &content, const QDateTime &timestamp, int messageId);
    static QJsonObject createPrivateMessageSent(const QString &to, int messageId, const QDateTime &timestamp);
    static QJsonObject createConversationRequest(const QString &with, int limit, int beforeMessageId);
    static QJsonObject createConversationResponse(const QString &with, const QJsonArray &messages);

    //Misc
    static QJsonObject createErrorMessage(int code, const QString &message);
    static QJsonObject createUserListMessage(const QStringList &users);

    //Public chat
    static QJsonObject createChatMessage(int messageId, int roomId, const QString &sender, const QString &content, const QDateTime &timestamp);
    static QJsonObject createSendMessage(int roomId, const QString &content);
    static QJsonObject createJoinRoomMessage(int roomId);
    static QJsonObject createLeaveRoomMessage(int roomId);
    static QJsonObject createRoomJoinedMessage(int roomId, const QString &roomName);
    static QJsonObject createRoomLeftMessage(int roomId);
    static QJsonObject createHistoryRequestMessage(int roomId, int limit = 50, int beforeMessageId = -1);
    static QJsonObject createHistoryResponseMessage(int roomId, const QJsonArray &messages);
    static QJsonObject createEditMessageMessage(int messageId, const QString &content);
    static QJsonObject createDeleteMessageMessage(int messageId);
    static QJsonObject createMessageEditedMessage(int messageId, const QString &content, const QDateTime &editedAt);
    static QJsonObject createMessageDeletedMessage(int messageId);

    //Presence
    static QJsonObject createPresenceSubscribeMessage();
    static QJsonObject createPresenceUpdateMessage(const QString &userId, const QString &username, bool online, qint64 lastSeen);
    static QJsonObject createPresenceListMessage(const QJsonArray &users);

    //Read/Unread receipts
    static QJsonObject createMarkReadMessage(const QString &with, int beforeMessageId);
    static QJsonObject createReadReceiptMessage(const QString &from, int beforeMessageId, const QDateTime &readAt);
    static QJsonObject createUnreadRequestMessage();
    static QJsonObject createUnreadSummaryMessage(const QJsonArray &conversations);

    //Parsing
    static MessageType getMessageType(const QJsonObject &message);
    static bool isValidMessage(const QJsonObject &message);

    // Parse specific
    static QString getUsername(const QJsonObject &message);
    static QString getPassword(const QJsonObject &message);
    static QString getContent(const QJsonObject &message);
    static QString getRecipient(const QJsonObject &message);
    static QString getConversationPartner(const QJsonObject &message);
    static int getRoomId(const QJsonObject &message);
    static int getUserId(const QJsonObject &message);
    static int getMessageId(const QJsonObject &message);
    static int getLimit(const QJsonObject &message);
    static int getBeforeMessageId(const QJsonObject &message);
    static QString getSender(const QJsonObject &message);
    static QString getSenderName(const QJsonObject &message);
    static QDateTime getTimestamp(const QJsonObject &message);
    static QStringList getRooms(const QJsonObject &message);
    static QStringList getUsers(const QJsonObject &message);
    static QJsonArray getMessages(const QJsonObject &message);
    static QJsonArray getUsersArray(const QJsonObject &message);
    static int getEditMessageId(const QJsonObject &message);

private:
    static QString messageTypeToString(MessageType type);
    static MessageType stringToMessageType(const QString &type);
};


#endif // MESSAGEPROTOCOL_H
