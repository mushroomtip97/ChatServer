#ifndef PRIVATEMESSAGEREPOSITORY_H
#define PRIVATEMESSAGEREPOSITORY_H

#include "IPrivateMessageRepository.h"

#include <QSqlDatabase>

class PrivateMessageRepository : public IPrivateMessageRepository
{
public:
    explicit PrivateMessageRepository(QSqlDatabase db);

    bool saveMessage(PrivateMessage &message) override;
    QList<PrivateMessage> getConversation(int userId, int otherUserId, int limit = 50, int beforeMessageId = -1) override;
    int markAsRead(int recipientId, int senderId, int beforeMessageId) override;
    QList<UnreadSummary> getUnreadSummary(int recipientId) override;

private:
    QSqlDatabase m_db;
};

#endif // PRIVATEMESSAGEREPOSITORY_H