#include "ratelimiter.h"

#include <QDateTime>

void RateLimiter::setLimits(int authPerUserPerMinute,int authPerIpPerMinute, int registrationsPerIpPerMinute)
{
    m_authPerUserPerMinute = qMax(0, authPerUserPerMinute);
    m_authPerIpPerMinute = qMax(0, authPerIpPerMinute);
    m_registrationsPerIpPerMinute = qMax(0, registrationsPerIpPerMinute);
}

void RateLimiter::prune(QList<qint64> &timestamps, qint64 cutoff)
{
    while (!timestamps.isEmpty() && timestamps.first() < cutoff)
    {
        timestamps.removeFirst();
    }
}

bool RateLimiter::isAuthBlocked(const QString &username, const QString &ip)
{
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 60'000;

    const auto &byUser = m_authByUser[username];
    const auto &byIp = m_authByIp[ip];

    // Const iteration: prune pretty lazily in recordAuthFailure(). The lists are
    // append-only here, so counting the tail is sufficient and avoids
    // non-const mutation in a predicate.
    const auto countNewer = [cutoff](const QList<qint64> &list) {
        int count = 0;
        for (auto it = list.crbegin(); it != list.crend(); ++it)
        {
            if (*it < cutoff)
            {
                break;
            }
            ++count;
        }
        return count;
    };

    return countNewer(byUser) >= m_authPerUserPerMinute || countNewer(byIp) >= m_authPerIpPerMinute;
}

void RateLimiter::recordAuthFailure(const QString &username, const QString &ip)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = now - 60'000;

    auto &byUser = m_authByUser[username];
    prune(byUser, cutoff);
    byUser.append(now);

    auto &byIp = m_authByIp[ip];
    prune(byIp, cutoff);
    byIp.append(now);
}

void RateLimiter::recordAuthSuccess(const QString &username, const QString &ip)
{
    Q_UNUSED(ip);
    m_authByUser.remove(username);
}

bool RateLimiter::registerAttempt(const QString &ip)
{
    if (m_registrationsPerIpPerMinute <= 0)
    {
        return true;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = now - 60'000;

    auto &attempts = m_registrationsByIp[ip];
    prune(attempts, cutoff);

    if (attempts.size() >= m_registrationsPerIpPerMinute)
    {
        return false;
    }

    attempts.append(now);
    return true;
}

void RateLimiter::clear()
{
    m_authByUser.clear();
    m_authByIp.clear();
    m_registrationsByIp.clear();
}

