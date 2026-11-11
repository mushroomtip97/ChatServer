#include "AuthManager.h"

#include "PasswordHasher.h"
#include "TransactionGuard.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QDebug>

#include <sodium.h>

namespace{
constexpr int kMinUsernameLength = 3;
constexpr int kMaxUsernameLength = 32;
constexpr int kMinPasswordLength = 6;
constexpr int kMaxPasswordLength = 512;
} // namespace

AuthManager::AuthManager(QSqlDatabase db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

AuthResult AuthManager::authenticate(const QString &username, const QString &password)
{
    AuthResult result;

    if (username.isEmpty() || password.isEmpty())
    {
        result.errorMessage = "Username and password are required";
        return result;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT id, username, password_hash FROM users WHERE username = ?");
    query.addBindValue(username);

    if (!query.exec())
    {
        result.errorMessage = "Database error: " + query.lastError().text();
        return result;
    }

    if (!query.next())
    {
        result.errorMessage = "Invalid credentials";
        return result;
    }

    const int userId = query.value(0).toInt();
    const QString dbUsername = query.value(1).toString();
    const QString storedHash = query.value(2).toString();

    if (!PasswordHasher::verify(password, storedHash))
    {
        result.errorMessage = "Invalid credentials";
        return result;
    }

    result.success = true;
    result.userId = userId;
    result.username = dbUsername;

    // Upgrade legacy SHA-256 hashes to Argon2id after a successful login.
    // ClientConnection normally does this asynchronously; this path keeps the
    // standalone AuthManager behavior correct as well.
    if (!PasswordHasher::isArgon2Hash(storedHash))
    {
        const QString newHash = PasswordHasher::hash(password);
        if (!newHash.isEmpty())
        {
            updatePasswordHash(userId, newHash);
        }
    }

    return result;
}

AuthResult AuthManager::registerUser(const QString &username, const QString &password)
{
    const QString validationError = validateRegistration(username, password);
    if (!validationError.isEmpty())
    {
        AuthResult result;
        result.errorMessage = validationError;
        return result;
    }

    return registerUserWithHash(username, PasswordHasher::hash(password));
}

QString AuthManager::validateUsername(const QString &username)
{
    if (username.length() < kMinUsernameLength)
    {
        return "Username must be at least 3 characters";
    }
    if (username.length() > kMaxUsernameLength)
    {
        return QString("Username must be at most %1 characters").arg(kMaxUsernameLength);
    }
    if (!QRegularExpression("^[a-zA-Z0-9_]+$").match(username).hasMatch())
    {
        return "Username can only contain letters, numbers, and underscores";
    }
    return {};
}

QString AuthManager::validateRegistration(const QString &username, const QString &password)
{
    const QString usernameError = validateUsername(username);
    if (!usernameError.isEmpty())
    {
        return usernameError;
    }
    if (password.length() < kMinPasswordLength)
    {
        return "Password must be at least 6 characters";
    }
    if (password.length() > kMaxPasswordLength)
    {
        return QString("Password must be at most %1 characters").arg(kMaxPasswordLength);
    }
    return {};
}

AuthResult AuthManager::registerUserWithHash(const QString &username, const QString &passwordHash)
{
    AuthResult result;

    const QString usernameError = validateUsername(username);
    if (!usernameError.isEmpty())
    {
        result.errorMessage = usernameError;
        return result;
    }

    if (passwordHash.isEmpty())
    {
        result.errorMessage = "Password hashing failed";
        return result;
    }

    if (userExists(username))
    {
        result.errorMessage = "Username already exists";
        return result;
    }

    TransactionGuard tg{m_db};
    if (!tg.active())
    {
        result.errorMessage = "Failed to start transaction";
        return result;
    }

    QSqlQuery insertUser(m_db);
    insertUser.prepare("INSERT INTO users (username, password_hash) VALUES (?, ?)");
    insertUser.addBindValue(username);
    insertUser.addBindValue(passwordHash);

    if (!insertUser.exec())
    {
        result.errorMessage = "Failed to create user: " + insertUser.lastError().text();
        return result;
    }

    const int newId = insertUser.lastInsertId().toInt();

    QSqlQuery addRoom(m_db);
    addRoom.prepare("INSERT INTO room_members (room_id, user_id) VALUES (1, ?)");
    addRoom.addBindValue(newId);

    if (!addRoom.exec())
    {
        result.errorMessage = "Failed to add user to default room: " + addRoom.lastError().text();
        return result;
    }

    if (!tg.commit())
    {
        result.errorMessage = "Failed to commit transaction";
        return result;
    }

    result.success = true;
    result.userId = newId;
    result.username = username;

    qInfo() << "User registered:" << username << "(ID:" << result.userId << ")";
    return result;
}

bool AuthManager::userExists(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM users WHERE username = ?");
    query.addBindValue(username);

    if (!query.exec() || !query.next())
    {
        return false;
    }

    return query.value(0).toInt() > 0;
}

bool AuthManager::updatePasswordHash(int userId, const QString &newHash)
{
    QSqlQuery query{m_db};
    query.prepare("UPDATE users SET password_hash = ? WHERE id = ?");
    query.addBindValue(newHash);
    query.addBindValue(userId);

    if (!query.exec())
    {
        qCritical() << "Failed to update password hash for user" << userId << ":" << query.lastError().text();
        return false;
    }

    qInfo() << "Upgraded password hash to Argon2id for user: " << userId;
    return true;
}
