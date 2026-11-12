#ifndef DATABASEWORKER_H
#define DATABASEWORKER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <memory>

#include "databasemanager.h"
#include "IAuthenticator.h"
#include "IMessageRepository.h"
#include "IPrivateMessageRepository.h"

class QChronoTimer;

// Thread-affine SQLite worker.
//
// Move an instance of this class to a QThread. All repository / SQL access
// happens in that thread, and results are delivered back through the
// requestFinished() signal. Callers must not touch the database directly.
class DatabaseWorker : public QObject
{
    Q_OBJECT
public:
    enum class Operation
    {
        FindUserForAuth,
        RegisterUser,
        UpdatePasswordHash,
        GetUserRooms,
        SaveRoomMessage,
        GetRoomMessages,
        UpdateMessage,
        DeleteMessage,
        JoinRoom,
        LeaveRoom,
        SendPrivateMessage,
        GetConversation,
        MarkRead,
        GetUnreadSummary,
        GetUsernameById
    };

    explicit DatabaseWorker(QObject *parent = nullptr);

    static quint64 nextRequestId();
    bool databaseHealthy() const { return m_healthy.load(); }

public slots:
    bool initialize(const QString &dbPath, bool seedDemoUser);
    void close();
    void submitRequest(quint64 requestId, int operation, const QVariantMap &params);

signals:
    void requestFinished(quint64 requestId, int operation, const QVariantMap &result);

private:
    void markHealthy(bool healthy);
    QVariantMap execute(Operation operation, const QVariantMap &params);

    DatabaseManager m_dbManager;
    QChronoTimer *m_healthTimer = nullptr;
    std::unique_ptr<IAuthenticator> m_auth;
    std::unique_ptr<IMessageRepository> m_messages;
    std::unique_ptr<IPrivateMessageRepository> m_privateMessages;
    std::atomic<bool> m_healthy{false};
};

#endif // DATABASEWORKER_H
