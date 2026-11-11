#include "RoomManager.h"
#include "ClientConnection.h"
#include <QDebug>

RoomManager::RoomManager(QObject *parent)
    : QObject(parent)
{
}

bool RoomManager::addClientToRoom(const QString &roomId, ClientConnection *client)
{
    if (!client)
    {
        qWarning() << "Client cannot be added null";
        return false;
    }

    if (!m_roomMembers.contains(roomId))
    {
        m_roomMembers[roomId] = QSet<ClientConnection*>();
    }

    m_roomMembers[roomId].insert(client);

    return true;
}

bool RoomManager::isClientInRoom(const QString &roomId, ClientConnection *client)
{
    auto it = m_roomMembers.find(roomId);
    return it != m_roomMembers.end() && it.value().contains(client);
}

bool RoomManager::removeClientFromRoom(const QString &roomId, ClientConnection *client)
{
    if (!m_roomMembers.contains(roomId))
    {
        return false;
    }

    bool removed = m_roomMembers[roomId].remove(client);

    if (removed)
    {

        // Clean up empty rooms
        if (m_roomMembers[roomId].isEmpty())
        {
            m_roomMembers.remove(roomId);
        }
    }

    return removed;
}

void RoomManager::removeClientFromAllRooms(ClientConnection *client)
{
    QList<QString> roomsToRemove;

    for (auto it = m_roomMembers.begin(); it != m_roomMembers.end(); ++it)
    {
        it.value().remove(client);

        if (it.value().isEmpty())
        {
            roomsToRemove.append(it.key());
        }
    }

    for (const QString &roomId : roomsToRemove)
    {
        m_roomMembers.remove(roomId);
    }

}

void RoomManager::clear()
{
    m_roomMembers.clear();
}

void RoomManager::broadcastToRoom(const QString &roomId, const QJsonObject &message, ClientConnection *exclude)
{
    if (!m_roomMembers.contains(roomId))
    {
        return;
    }

    for (ClientConnection *client : m_roomMembers[roomId])
    {
        if (client && client != exclude)
        {
            client->sendMessage(message);
        }
    }

}

void RoomManager::sendToClient(const QString &roomId, ClientConnection *client, const QJsonObject &message)
{
    if (client && m_roomMembers.contains(roomId) && m_roomMembers[roomId].contains(client))
    {
        client->sendMessage(message);
    }
}

QSet<ClientConnection*> RoomManager::getClientsInRoom(const QString &roomId) const
{
    return m_roomMembers.value(roomId);
}

QStringList RoomManager::getRoomsForClient(ClientConnection *client) const
{
    QStringList rooms;

    for (auto it = m_roomMembers.begin(); it != m_roomMembers.end(); ++it)
    {
        if (it.value().contains(client))
        {
            rooms.append(it.key());
        }
    }

    return rooms;
}
