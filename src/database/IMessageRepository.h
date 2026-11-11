#ifndef IMESSAGEREPOSITORY_H
#define IMESSAGEREPOSITORY_H

#include "Message.h"

#include <QList>
#include <QString>

// Persistence boundary for public room messages.
class IMessageRepository
{
public:
    struct MessageOwnership
    {
        int userId = -1;
        int roomId = -1;
        bool deleted = false;
        bool found = false;
    };

    virtual ~IMessageRepository() = default;

    virtual bool saveMessage(Message &message) = 0;
    virtual QList<Message> getRoomMessages(int roomId, int limit, int beforeMessageId) = 0;
    virtual bool updateMessage(int messageId, const QString &newContent) = 0;
    virtual bool softDeleteMessage(int messageId) = 0;
    virtual MessageOwnership lookupMessage(int messageId) = 0;
};

#endif // IMESSAGEREPOSITORY_H