#ifndef PRESENCEMANAGER_H
#define PRESENCEMANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>

class ClientConnection;

class PresenceManager : public QObject
{
    Q_OBJECT
public:
    explicit PresenceManager(QObject *parent = nullptr);

    bool handleUserConnected(const QString &userId, ClientConnection *connection);
    bool handleUserDisconnected(const QString &userId, ClientConnection *connection);

    QDateTime getUserLastSeen(const QString &userId) const;
    QStringList onlineUserIds() const;

    bool isOnline(const QString &userId)const;
    void clear();

signals:
    void presenceChanged(const QString &userId, bool online, qint64 lastSeen);

private:
    QHash<QString, QSet<ClientConnection*>> m_userConnections;
    QHash<QString, qint64> m_lastSeenMap;
};

#endif // PRESENCEMANAGER_H
