#ifndef MESSAGEREPOSITORY_H
#define MESSAGEREPOSITORY_H

#include "IMessageRepository.h"

#include <QSqlDatabase>

class MessageRepository : public IMessageRepository
{
public:
    explicit MessageRepository(QSqlDatabase db);

    bool saveMessage(Message &message) override;
    QList<Message> getRoomMessages(int roomId, int limit, int beforeMessageId) override;
    bool updateMessage(int messageId, const QString &newContent) override;
    bool softDeleteMessage(int messageId) override;
    MessageOwnership lookupMessage(int messageId) override;

private:
    QSqlDatabase m_db;
};

#endif // MESSAGEREPOSITORY_H