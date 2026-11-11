#ifndef USER_H
#define USER_H

#include <QString>
#include <QDateTime>

class User
{
private:
    int m_id{-1};
    QString name;
    QDateTime m_createdAt;

public:

    User() = default;

    int id() const;
    void setId(int newId);
    QString getName() const;
    void setName(const QString &newName);
    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &newCreatedAt);
};

inline QString User::getName() const
{
    return name;
}

inline void User::setName(const QString &newName)
{
    name = newName;
}

inline QDateTime User::createdAt() const
{
    return m_createdAt;
}

inline void User::setCreatedAt(const QDateTime &newCreatedAt)
{
    m_createdAt = newCreatedAt;
}

inline int User::id() const
{
    return m_id;
}

inline void User::setId(int newId)
{
    m_id = newId;
}

#endif // USER_H
