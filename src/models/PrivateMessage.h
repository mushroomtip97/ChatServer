#ifndef PRIVATEMESSAGE_H
#define PRIVATEMESSAGE_H

#include <QString>
#include <QDateTime>

class PrivateMessage
{
public:
    PrivateMessage() = default;

    int id() const;
    void setId(int newId);

    int senderId() const;
    void setSenderId(int newSenderId);

    int recipientId() const;
    void setRecipientId(int newRecipientId);

    QString content() const;
    void setContent(const QString &newContent);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &newCreatedAt);

private:
    int m_id{-1};
    int m_senderId{-1};
    int m_recipientId{-1};
    QString m_content;
    QDateTime m_createdAt;
};

inline int PrivateMessage::id() const
{
    return m_id;
}

inline void PrivateMessage::setId(int newId)
{
    m_id = newId;
}

inline int PrivateMessage::senderId() const
{
    return m_senderId;
}

inline void PrivateMessage::setSenderId(int newSenderId)
{
    m_senderId = newSenderId;
}

inline int PrivateMessage::recipientId() const
{
    return m_recipientId;
}

inline void PrivateMessage::setRecipientId(int newRecipientId)
{
    m_recipientId = newRecipientId;
}

inline QString PrivateMessage::content() const
{
    return m_content;
}

inline void PrivateMessage::setContent(const QString &newContent)
{
    m_content = newContent;
}

inline QDateTime PrivateMessage::createdAt() const
{
    return m_createdAt;
}

inline void PrivateMessage::setCreatedAt(const QDateTime &newCreatedAt)
{
    m_createdAt = newCreatedAt;
}


#endif // PRIVATEMESSAGE_H
