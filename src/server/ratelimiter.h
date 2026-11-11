#ifndef RATELIMITER_H
#define RATELIMITER_H

#include <QHash>
#include <QString>

// Fixed-window rate limiter. All ChatServer/ClientConnection callers run on
// the Qt main thread, so no locking is required.
class RateLimiter
{
public:
    RateLimiter() = default;

    void setLimits(int authPerUserPerMinute,int authPerIpPerMinute, int registrationsPerIpPerMinute);

    bool isAuthBlocked(const QString &username, const QString &ip);
    void recordAuthFailure(const QString &username, const QString &ip);
    void recordAuthSuccess(const QString &username, const QString &ip);

    // Records the registration attempt immediately and returns false when the
    // per-IP registration budget is exhausted.
    bool registerAttempt(const QString &ip);

    void clear();

private:
    static void prune(QList<qint64> &timestamps, qint64 cutoff);

    int m_authPerUserPerMinute = 10;
    int m_authPerIpPerMinute = 30;
    int m_registrationsPerIpPerMinute = 0; // 0 = unlimited

    QHash<QString, QList<qint64>> m_authByUser;
    QHash<QString, QList<qint64>> m_authByIp;
    QHash<QString, QList<qint64>> m_registrationsByIp;
};

#endif // RATELIMITER_H