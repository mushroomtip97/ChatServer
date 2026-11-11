#ifndef MESSAGE_H
#define MESSAGE_H

#include <QString>
#include <QDateTime>




class Message
{
private:
    int m_id{-1};
    int m_roomId{-1};
    int m_userId{-1};

    QString m_content;
    QDateTime m_createdAt;

    QDateTime m_editedAt;
    QDateTime m_deletedAt;

public:
    Message() = default;

    int id() const;
    void setId(int newId);
    int roomId() const;
    void setRoomId(int newRoomId);
    int userId() const;
    void setUserId(int newUserId);
    QString content() const;
    void setContent(const QString &newContent);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &newCreatedAt);

    QDateTime editedAt()const;
    void setEditedAt(const QDateTime &newEditedAt);

    QDateTime deletedAt()const;
    void setDeletedAt(const QDateTime &newDeletedAt);
};

inline int Message::roomId() const
{
    return m_roomId;
}

inline void Message::setRoomId(int newRoomId)
{
    m_roomId = newRoomId;
}

inline int Message::userId() const
{
    return m_userId;
}

inline void Message::setUserId(int newUserId)
{
    m_userId = newUserId;
}

inline QString Message::content() const
{
    return m_content;
}

inline void Message::setContent(const QString &newContent)
{
    m_content = newContent;
}


inline QDateTime Message::createdAt() const
{
    return m_createdAt;
}

inline void Message::setCreatedAt(const QDateTime &newCreatedAt)
{
    m_createdAt = newCreatedAt;
}

inline QDateTime Message::editedAt()const
{
    return m_editedAt;
}

inline void Message::setEditedAt(const QDateTime &newEditedAt)
{
    m_editedAt = newEditedAt;
}
inline QDateTime Message::deletedAt()const
{
    return m_deletedAt;
}

inline void Message::setDeletedAt(const QDateTime &newDeletedAt)
{
    m_deletedAt = newDeletedAt;
}

inline int Message::id() const
{
    return m_id;
}

inline void Message::setId(int newId)
{
    m_id = newId;
}

#endif // MESSAGE_H
