#ifndef ROOMMANAGER_H
#define ROOMMANAGER_H



#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QJsonObject>

class ClientConnection;

class RoomManager : public QObject
{
    Q_OBJECT
public:
    explicit RoomManager(QObject *parent = nullptr);

    bool addClientToRoom(const QString &roomId, ClientConnection *client);
    bool isClientInRoom(const QString &roomId, ClientConnection *client);
    bool removeClientFromRoom(const QString &roomId, ClientConnection *client);
    void removeClientFromAllRooms(ClientConnection *client);
    void clear();

    void broadcastToRoom(const QString &roomId, const QJsonObject &message, ClientConnection *exclude = nullptr);
    void sendToClient(const QString &roomId, ClientConnection *client, const QJsonObject &message);

    QSet<ClientConnection*> getClientsInRoom(const QString &roomId) const;
    QStringList getRoomsForClient(ClientConnection *client) const;
    QSet<QString> getRoomIds() const
    {
        return QSet<QString>(m_roomMembers.keys().begin(), m_roomMembers.keys().end());
    }

private:
    QMap<QString, QSet<ClientConnection*>> m_roomMembers;
};

#endif // ROOMMANAGER_H


