#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include "IAuthenticator.h"

#include <QObject>
#include <QString>
#include <QSqlDatabase>

class AuthManager : public QObject, public IAuthenticator
{
    Q_OBJECT

public:
    explicit AuthManager(QSqlDatabase db, QObject *parent = nullptr);

    AuthResult authenticate(const QString &username, const QString &password) override;
    AuthResult registerUserWithHash(const QString &username, const QString &passwordHash) override;
    bool updatePasswordHash(int userId, const QString &newHash) override;

    // Kept for callers that are not performance-sensitive. ClientConnection
    // uses PasswordHasher + registerUserWithHash() so Argon2 runs off-thread.
    AuthResult registerUser(const QString &username, const QString &password);
    bool userExists(const QString &username);

    // Returns an empty string when input is valid, otherwise a client-safe error.
    static QString validateUsername(const QString &username);
    static QString validateRegistration(const QString &username, const QString &password);

private:
    QSqlDatabase m_db;
};

#endif // AUTHMANAGER_H