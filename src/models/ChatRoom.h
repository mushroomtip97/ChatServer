#ifndef CHATROOM_H
#define CHATROOM_H

#include <QString>
#include <QDateTime>

class ChatRoom
{
private:
    int m_id{-1};
    QString m_name;
    QDateTime m_createdAt;

public:
    ChatRoom() = default;

    int id() const;
    void setId(int newId);
    QString name() const;
    void setName(const QString &newName);
    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &newCreated_At);
};

inline QString ChatRoom::name() const
{
    return m_name;
}

inline void ChatRoom::setName(const QString &newName)
{
    m_name = newName;
}

inline QDateTime ChatRoom::createdAt() const
{
    return m_createdAt;
}

inline void ChatRoom::setCreatedAt(const QDateTime &newCreated_At)
{
    m_createdAt = newCreated_At;
}

inline int ChatRoom::id() const
{
    return m_id;
}

inline void ChatRoom::setId(int newId)
{
    m_id = newId;
}

#endif // CHATROOM_H
