#include "presencemanager.h"
#include "clientconnection.h"
#include "messageprotocol.h"

PresenceManager::PresenceManager(QObject *parent)
    : QObject{parent}
{}

bool PresenceManager::handleUserConnected(const QString &userId, ClientConnection *connection)
{
    if(userId.isEmpty() || !connection)
    {
        return false;
    }

    auto &connections = m_userConnections[userId];
    const bool wasOffline = connections.isEmpty();
    connections.insert(connection);


    if(wasOffline)
    {
        qInfo() << "User: " << userId << " is now online";
        emit presenceChanged(userId, true, 0);
    }

    return wasOffline;

}

bool PresenceManager::handleUserDisconnected(const QString &userId, ClientConnection *connection)
{
    if(userId.isEmpty() || !connection)
    {
        return false;
    }

    auto it = m_userConnections.find(userId);
    if(it == m_userConnections.end())
    {
        return false;
    }
    it.value().remove(connection);
    if(it->isEmpty())
    {
        auto now = QDateTime::currentSecsSinceEpoch();
        m_lastSeenMap[userId] = now;
        m_userConnections.erase(it);

        qInfo() << "User: " << userId << " is now offline";
        emit presenceChanged(userId, false, now);
        return true;
    }

    return false;
}

bool PresenceManager::isOnline(const QString &userId) const
{
    return !m_userConnections.value(userId).isEmpty();
}


QDateTime PresenceManager::getUserLastSeen(const QString &userId) const
{
    auto it = m_lastSeenMap.find(userId);
    if(it != m_lastSeenMap.end())
    {
        return QDateTime::fromSecsSinceEpoch(it.value());
    }
    return QDateTime();
}

void PresenceManager::clear()
{
    m_userConnections.clear();
    m_lastSeenMap.clear();
}

QStringList PresenceManager::onlineUserIds() const
{
    return m_userConnections.keys();
}


