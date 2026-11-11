#ifndef AUTHRESULT_H
#define AUTHRESULT_H

#include <QString>

struct AuthResult
{
    bool success = false;
    int userId = -1;
    QString username;
    QString errorMessage;
};

#endif // AUTHRESULT_H