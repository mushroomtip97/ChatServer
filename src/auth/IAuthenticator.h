#ifndef IAUTHENTICATOR_H
#define IAUTHENTICATOR_H

#include "AuthResult.h"

#include <QString>

// Authentication boundary. The server depends on this interface so password
// auth can be replaced with token/OAuth auth, or mocked in tests.
class IAuthenticator
{
public:
    virtual ~IAuthenticator() = default;

    virtual AuthResult authenticate(const QString &username, const QString &password) = 0;
    virtual AuthResult registerUserWithHash(const QString &username, const QString &passwordHash) = 0;
    virtual bool updatePasswordHash(int userId, const QString &newHash) = 0;
};

#endif // IAUTHENTICATOR_H