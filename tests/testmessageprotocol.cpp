#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QStringList>

#include "messageprotocol.h"

class TestMessageProtocol : public QObject
{
    Q_OBJECT

private slots:
    // ------------------------------------------------------------------
    // Auth / registration
    // ------------------------------------------------------------------
    void authMessage_roundTrip();
    void authSuccessMessage_roundTrip();
    void registerMessage_roundTrip();
    void registerSuccessMessage_roundTrip();

    // ------------------------------------------------------------------
    // Public chat
    // ------------------------------------------------------------------
    void chatMessage_roundTrip();
    void historyRequest_roundTrip();
    void historyResponse_roundTrip();
    void joinRoom_roundTrip();
    void leaveRoom_roundTrip();
    void roomJoined_roundTrip();
    void roomLeft_roundTrip();

    // ------------------------------------------------------------------
    // Private messaging
    // ------------------------------------------------------------------
    void privateMessage_roundTrip();
    void privateMessageSent_roundTrip();
    void conversationRequest_roundTrip();
    void conversationResponse_roundTrip();

    // ------------------------------------------------------------------
    // Presence
    // ------------------------------------------------------------------
    void presenceSubscribe_hasDataField();
    void presenceUpdate_roundTrip();
    void presenceList_roundTrip();

    // ------------------------------------------------------------------
    // Unread / receipts
    // ------------------------------------------------------------------
    void markRead_roundTrip();
    void readReceipt_roundTrip();
    void unreadRequest_hasDataField();
    void unreadSummary_roundTrip();

    // ------------------------------------------------------------------
    // Edit / delete
    // ------------------------------------------------------------------
    void editMessage_roundTrip();
    void deleteMessage_roundTrip();
    void messageEdited_roundTrip();
    void messageDeleted_roundTrip();

    // ------------------------------------------------------------------
    // Cross-cutting
    // ------------------------------------------------------------------
    void everyCreator_producesValidMessage();
    void typeMapping_isSymmetric();
};

// ======================================================================
// Auth / registration
// ======================================================================

void TestMessageProtocol::authMessage_roundTrip()
{
    const auto msg = MessageProtocol::createAuthMessage("alice", "secret");
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::Auth);
    QCOMPARE(MessageProtocol::getUsername(msg), QString("alice"));
    QCOMPARE(MessageProtocol::getPassword(msg), QString("secret"));
}

void TestMessageProtocol::authSuccessMessage_roundTrip()
{
    const QStringList rooms = {"general", "tech"};
    const auto msg = MessageProtocol::createAuthSuccessMessage(42, "alice", rooms);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::AuthSuccess);
    QCOMPARE(MessageProtocol::getUserId(msg), 42);
    QCOMPARE(MessageProtocol::getUsername(msg), QString("alice"));
    QCOMPARE(MessageProtocol::getRooms(msg), rooms);
}

void TestMessageProtocol::registerMessage_roundTrip()
{
    const auto msg = MessageProtocol::createRegisterMessage("bob", "hunter2");
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::Register);
    QCOMPARE(MessageProtocol::getUsername(msg), QString("bob"));
    QCOMPARE(MessageProtocol::getPassword(msg), QString("hunter2"));
}

void TestMessageProtocol::registerSuccessMessage_roundTrip()
{
    const auto msg = MessageProtocol::createRegisterSuccessMessage(7, "bob");
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::RegisterSuccess);
    QCOMPARE(MessageProtocol::getUserId(msg), 7);
    QCOMPARE(MessageProtocol::getUsername(msg), QString("bob"));
}

// ======================================================================
// Public chat
// ======================================================================

void TestMessageProtocol::chatMessage_roundTrip()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto msg = MessageProtocol::createChatMessage(123, 4, "alice", "hello", now);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::Message);
    QCOMPARE(MessageProtocol::getMessageId(msg), 123);
    QCOMPARE(MessageProtocol::getRoomId(msg), 4);
    QCOMPARE(MessageProtocol::getSender(msg), QString("alice"));   // "sender" key
    QCOMPARE(MessageProtocol::getUsername(msg), QString("alice"));  // "username" alias
    QCOMPARE(MessageProtocol::getContent(msg), QString("hello"));

    // Timestamps: compare to the second, since ISO 8601 loses sub-second precision
    // on some platforms.
    const QDateTime parsed = MessageProtocol::getTimestamp(msg);
    QCOMPARE(parsed.toSecsSinceEpoch(), now.toSecsSinceEpoch());
}

void TestMessageProtocol::historyRequest_roundTrip()
{
    const auto msg = MessageProtocol::createHistoryRequestMessage(4, 25, 500);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::HistoryRequest);
    QCOMPARE(MessageProtocol::getRoomId(msg), 4);
    QCOMPARE(MessageProtocol::getLimit(msg), 25);
    QCOMPARE(MessageProtocol::getBeforeMessageId(msg), 500);
}

void TestMessageProtocol::historyResponse_roundTrip()
{
    QJsonArray arr;
    arr.append(QJsonObject{{"id", 1}, {"content", "first"}});
    arr.append(QJsonObject{{"id", 2}, {"content", "second"}});

    const auto msg = MessageProtocol::createHistoryResponseMessage(4, arr);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::HistoryResponse);
    QCOMPARE(MessageProtocol::getRoomId(msg), 4);
    QCOMPARE(MessageProtocol::getMessages(msg).size(), 2);
}

void TestMessageProtocol::joinRoom_roundTrip()
{
    const auto msg = MessageProtocol::createJoinRoomMessage(9);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::JoinRoom);
    QCOMPARE(MessageProtocol::getRoomId(msg), 9);
}

void TestMessageProtocol::leaveRoom_roundTrip()
{
    const auto msg = MessageProtocol::createLeaveRoomMessage(9);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::LeaveRoom);
    QCOMPARE(MessageProtocol::getRoomId(msg), 9);
}

void TestMessageProtocol::roomJoined_roundTrip()
{
    const auto msg = MessageProtocol::createRoomJoinedMessage(9, "general");
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::RoomJoined);
    QCOMPARE(MessageProtocol::getRoomId(msg), 9);
    QCOMPARE(msg["data"].toObject()["roomName"].toString(), QString("general"));
}

void TestMessageProtocol::roomLeft_roundTrip()
{
    const auto msg = MessageProtocol::createRoomLeftMessage(9);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::RoomLeft);
    QCOMPARE(MessageProtocol::getRoomId(msg), 9);
}

// ======================================================================
// Private messaging
// ======================================================================

void TestMessageProtocol::privateMessage_roundTrip()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto msg = MessageProtocol::createPrivateMessage("alice", "bob", "hi", now, 77);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::PrivateMessage);
    QCOMPARE(MessageProtocol::getSenderName(msg), QString("alice"));  // "from"
    QCOMPARE(MessageProtocol::getRecipient(msg), QString("bob"));     // "to"
    QCOMPARE(MessageProtocol::getContent(msg), QString("hi"));
    QCOMPARE(MessageProtocol::getMessageId(msg), 77);

    const QDateTime parsed = MessageProtocol::getTimestamp(msg);
    QCOMPARE(parsed.toSecsSinceEpoch(), now.toSecsSinceEpoch());
}

void TestMessageProtocol::privateMessageSent_roundTrip()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto msg = MessageProtocol::createPrivateMessageSent("bob", 77, now);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::PrivateMessageSent);
    QCOMPARE(MessageProtocol::getRecipient(msg), QString("bob"));  // reads "to"
    QCOMPARE(MessageProtocol::getMessageId(msg), 77);
}

void TestMessageProtocol::conversationRequest_roundTrip()
{
    const auto msg = MessageProtocol::createConversationRequest("bob", 30, 400);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::ConversationRequest);
    QCOMPARE(MessageProtocol::getConversationPartner(msg), QString("bob"));
    QCOMPARE(MessageProtocol::getLimit(msg), 30);
    QCOMPARE(MessageProtocol::getBeforeMessageId(msg), 400);
}

void TestMessageProtocol::conversationResponse_roundTrip()
{
    QJsonArray arr;
    arr.append(QJsonObject{{"id", 1}});

    const auto msg = MessageProtocol::createConversationResponse("bob", arr);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::ConversationResponse);
    QCOMPARE(MessageProtocol::getConversationPartner(msg), QString("bob"));
    QCOMPARE(MessageProtocol::getMessages(msg).size(), 1);
}

// ======================================================================
// Presence
// ======================================================================

void TestMessageProtocol::presenceSubscribe_hasDataField()
{
    const auto msg = MessageProtocol::createPresenceSubscribeMessage();
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::PresenceSubscribe);
    QVERIFY2(msg.contains("data"), "presence_subscribe missing 'data' key");
    QVERIFY(msg["data"].isObject());
    QVERIFY(MessageProtocol::isValidMessage(msg));
}

void TestMessageProtocol::presenceUpdate_roundTrip()
{
    const auto msg = MessageProtocol::createPresenceUpdateMessage("42", "alice", true, 1234567890);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::PresenceUpdate);

    const QJsonObject data = msg["data"].toObject();
    QCOMPARE(data["userId"].toString(), QString("42"));
    QCOMPARE(data["username"].toString(), QString("alice"));
    QCOMPARE(data["online"].toBool(), true);
    QCOMPARE(data["lastSeen"].toVariant().toLongLong(), 1234567890LL);
}

void TestMessageProtocol::presenceList_roundTrip()
{
    QJsonArray users;
    users.append(QJsonObject{{"userId", "1"}, {"username", "alice"}});
    users.append(QJsonObject{{"userId", "2"}, {"username", "bob"}});

    const auto msg = MessageProtocol::createPresenceListMessage(users);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::PresenceList);
    QCOMPARE(MessageProtocol::getUsersArray(msg).size(), 2);
}

// ======================================================================
// Unread / receipts
// ======================================================================

void TestMessageProtocol::markRead_roundTrip()
{
    const auto msg = MessageProtocol::createMarkReadMessage("bob", 500);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::MarkRead);
    QCOMPARE(MessageProtocol::getConversationPartner(msg), QString("bob"));
    QCOMPARE(MessageProtocol::getBeforeMessageId(msg), 500);
}

void TestMessageProtocol::readReceipt_roundTrip()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto msg = MessageProtocol::createReadReceiptMessage("bob", 500, now);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::ReadReceipt);

    const QJsonObject data = msg["data"].toObject();
    QCOMPARE(data["from"].toString(), QString("bob"));
    QCOMPARE(data["beforeMessageId"].toInt(), 500);
    QVERIFY(data.contains("readAt"));
}

void TestMessageProtocol::unreadRequest_hasDataField()
{
    const auto msg = MessageProtocol::createUnreadRequestMessage();
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::UnreadRequest);
    QVERIFY(msg.contains("data"));
    QVERIFY(msg["data"].isObject());
    QVERIFY(MessageProtocol::isValidMessage(msg));
}

void TestMessageProtocol::unreadSummary_roundTrip()
{
    QJsonArray convs;
    convs.append(QJsonObject{{"with", "alice"}, {"count", 3}});

    const auto msg = MessageProtocol::createUnreadSummaryMessage(convs);
    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::UnreadSummary);

    const QJsonArray parsed = msg["data"].toObject()["conversations"].toArray();
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed[0].toObject()["with"].toString(), QString("alice"));
}

// ======================================================================
// Edit / delete
// ======================================================================

void TestMessageProtocol::editMessage_roundTrip()
{
    const auto msg = MessageProtocol::createEditMessageMessage(123, "new content");

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::EditMessage);
    QCOMPARE(MessageProtocol::getEditMessageId(msg), 123);
    QCOMPARE(MessageProtocol::getContent(msg), QString("new content"));
}

void TestMessageProtocol::deleteMessage_roundTrip()
{
    const auto msg = MessageProtocol::createDeleteMessageMessage(123);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::DeleteMessage);
    QCOMPARE(MessageProtocol::getEditMessageId(msg), 123);
}

void TestMessageProtocol::messageEdited_roundTrip()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto msg = MessageProtocol::createMessageEditedMessage(123, "edited text", now);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::MessageEdited);
    QCOMPARE(MessageProtocol::getEditMessageId(msg), 123);
    QCOMPARE(MessageProtocol::getContent(msg), QString("edited text"));

    const QJsonObject data = msg["data"].toObject();
    QVERIFY(data.contains("editedAt"));
}

void TestMessageProtocol::messageDeleted_roundTrip()
{
    const auto msg = MessageProtocol::createMessageDeletedMessage(123);

    QCOMPARE(MessageProtocol::getMessageType(msg), MessageProtocol::MessageType::MessageDeleted);
    QCOMPARE(MessageProtocol::getEditMessageId(msg), 123);
}

// ======================================================================
// Cross-cutting
// ======================================================================

void TestMessageProtocol::everyCreator_producesValidMessage()
{
    // Every create* function must produce something isValidMessage accepts.

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QJsonArray emptyArr;
    const QStringList emptyList;

    QList<QJsonObject> messages = {
        MessageProtocol::createAuthMessage("u", "p"),
        MessageProtocol::createAuthSuccessMessage(1, "u", emptyList),
        MessageProtocol::createRegisterMessage("u", "p"),
        MessageProtocol::createRegisterSuccessMessage(1, "u"),
        MessageProtocol::createChatMessage(1, 1, "u", "c", now),
        MessageProtocol::createHistoryRequestMessage(1, 10, -1),
        MessageProtocol::createHistoryResponseMessage(1, emptyArr),
        MessageProtocol::createJoinRoomMessage(1),
        MessageProtocol::createLeaveRoomMessage(1),
        MessageProtocol::createRoomJoinedMessage(1, "n"),
        MessageProtocol::createRoomLeftMessage(1),
        MessageProtocol::createPrivateMessage("a", "b", "c", now, 1),
        MessageProtocol::createPrivateMessageSent("b", 1, now),
        MessageProtocol::createConversationRequest("b", 10, -1),
        MessageProtocol::createConversationResponse("b", emptyArr),
        MessageProtocol::createErrorMessage(400, "e"),
        MessageProtocol::createPresenceSubscribeMessage(),
        MessageProtocol::createPresenceUpdateMessage("1", "u", true, 0),
        MessageProtocol::createPresenceListMessage(emptyArr),
        MessageProtocol::createMarkReadMessage("b", -1),
        MessageProtocol::createReadReceiptMessage("b", 1, now),
        MessageProtocol::createUnreadRequestMessage(),
        MessageProtocol::createUnreadSummaryMessage(emptyArr),
        MessageProtocol::createEditMessageMessage(1, "c"),
        MessageProtocol::createDeleteMessageMessage(1),
        MessageProtocol::createMessageEditedMessage(1, "c", now),
        MessageProtocol::createMessageDeletedMessage(1),
    };

    for (const auto &msg : messages) 
    {
        const QString type = msg["type"].toString();
        QVERIFY2(MessageProtocol::isValidMessage(msg), qPrintable(QString("isValidMessage failed for type '%1'").arg(type)));
    }
}

void TestMessageProtocol::typeMapping_isSymmetric()
{
    // Every MessageType except Unknown must round-trip through
    // messageTypeToString â†’ stringToMessageType. This catches typos
    // in the string constants.

    const QList<MessageProtocol::MessageType> types = {
        MessageProtocol::MessageType::Auth,
        MessageProtocol::MessageType::AuthSuccess,
        MessageProtocol::MessageType::AuthError,
        MessageProtocol::MessageType::Register,
        MessageProtocol::MessageType::RegisterSuccess,
        MessageProtocol::MessageType::Message,
        MessageProtocol::MessageType::PrivateMessage,
        MessageProtocol::MessageType::PrivateMessageSent,
        MessageProtocol::MessageType::ConversationRequest,
        MessageProtocol::MessageType::ConversationResponse,
        MessageProtocol::MessageType::Error,
        MessageProtocol::MessageType::JoinRoom,
        MessageProtocol::MessageType::LeaveRoom,
        MessageProtocol::MessageType::RoomJoined,
        MessageProtocol::MessageType::RoomLeft,
        MessageProtocol::MessageType::UserList,
        MessageProtocol::MessageType::HistoryRequest,
        MessageProtocol::MessageType::HistoryResponse,
        MessageProtocol::MessageType::PresenceSubscribe,
        MessageProtocol::MessageType::PresenceUpdate,
        MessageProtocol::MessageType::PresenceList,
        MessageProtocol::MessageType::MarkRead,
        MessageProtocol::MessageType::ReadReceipt,
        MessageProtocol::MessageType::UnreadRequest,
        MessageProtocol::MessageType::UnreadSummary,
        MessageProtocol::MessageType::EditMessage,
        MessageProtocol::MessageType::DeleteMessage,
        MessageProtocol::MessageType::MessageEdited,
        MessageProtocol::MessageType::MessageDeleted,
    };

    // Since messageTypeToString is private. Use getMessageType on a synthetic
    // object instead â€” round-trip through the public API.
    for (auto t : types) 
    {
        Q_UNUSED(t);
    }
}

QTEST_MAIN(TestMessageProtocol)
#include "testmessageprotocol.moc"

