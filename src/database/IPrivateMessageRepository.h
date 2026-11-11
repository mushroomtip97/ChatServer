#ifndef IPRIVATEMESSAGEREPOSITORY_H
#define IPRIVATEMESSAGEREPOSITORY_H

#include "PrivateMessage.h"

#include <QDateTime>
#include <QList>
#include <QString>

// Persistence boundary for private messages.
class IPrivateMessageRepository
{
public:
    struct UnreadSummary
    {
        int senderId = -1;
        QString senderName;
        int unreadCount = 0;
        QDateTime lastMessageAt;
    };

    virtual ~IPrivateMessageRepository() = default;

    virtual bool saveMessage(PrivateMessage &message) = 0;
    virtual QList<PrivateMessage> getConversation(int userId, int otherUserId, int limit = 50, int beforeMessageId = -1) = 0;
    virtual int markAsRead(int recipientId, int senderId, int beforeMessageId) = 0;
    virtual QList<UnreadSummary> getUnreadSummary(int recipientId) = 0;
};

#endif // IPRIVATEMESSAGEREPOSITORY_H