#ifndef PASSWORDHASHER_H
#define PASSWORDHASHER_H

#include <QString>
#include <QThreadPool>

// Thread-safe password hashing / verification helpers.
//
// Argon2id hashing and verification must be
// called from worker threads (QtConcurrent::run with PasswordHasher::pool()),
// never from the Qt main/event-loop thread.
class PasswordHasher
{
public:
    static QThreadPool &pool();

    static QString hash(const QString &password);
    static bool verify(const QString &password, const QString &storedHash);

    static bool isArgon2Hash(const QString &hash);
    static bool verifyLegacySha256(const QString &password, const QString &storedHash);

    PasswordHasher() = delete;
};

#endif // PASSWORDHASHER_H
