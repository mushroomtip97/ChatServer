#include "PasswordHasher.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QThread>

#include <sodium.h>

namespace {
constexpr int kMaxPoolThreads = 4;

// Valid Argon2id hash used to burn the same CPU/memory time when a caller
// supplies an empty or malformed stored hash. The actual password for this
// hash is intentionally irrelevant.
constexpr char kDummyArgon2Hash[] =
    "$argon2id$v=19$m=65536,t=2,p=1$ytYrXpkkJY/d9oOMGOqlcQ$kPXJeDvYvkV7b4hejw2slv9H9mCIShaxjkALQBxq9GQ";

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty())
    {
        sodium_memzero(bytes.data(), static_cast<size_t>(bytes.size()));
    }
}
} // namespace

QThreadPool &PasswordHasher::pool()
{
    static QThreadPool threadPool;
    static const bool configured = []()
    {
        const int ideal = QThread::idealThreadCount();
        const int workers = qBound(1, ideal > 0 ? ideal / 2 : 1, kMaxPoolThreads);
        threadPool.setMaxThreadCount(workers);
        return true;
    }();
    Q_UNUSED(configured);
    return threadPool;
}

QString PasswordHasher::hash(const QString &password)
{
    if (password.isEmpty())
    {
        return {};
    }

    QByteArray pass = password.toUtf8();
    char hashed[crypto_pwhash_STRBYTES];
    const int hashResult = crypto_pwhash_str(
        hashed,
        pass.constData(),
        static_cast<unsigned long long>(pass.size()),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE);

    // libsodium no longer needs the plaintext. Wipe it before checking the
    // result so the password is cleared on both success and failure paths.
    wipe(pass);

    if (hashResult != 0)
    {
        qCritical() << "Argon2id hashing failed (out of memory?)";
        return {};
    }

    const QString result = QString::fromUtf8(hashed);
    sodium_memzero(hashed, sizeof(hashed));
    return result;
    return result;
}

bool PasswordHasher::verify(const QString &password, const QString &storedHash)
{
    QByteArray pass = password.toUtf8();
    QByteArray stored = storedHash.toUtf8();

    bool ok = false;

    if (isArgon2Hash(storedHash))
    {
        ok = crypto_pwhash_str_verify(
                 stored.constData(),
                 pass.constData(),
                 static_cast<unsigned long long>(pass.size())) == 0;
    }
    else if (storedHash.isEmpty())
    {
        // Burns the same Argon2 time as a real verification so an empty
        // stored hash is not observable through timing.
        const int dummyResult = crypto_pwhash_str_verify(
            kDummyArgon2Hash,
            pass.constData(),
            static_cast<unsigned long long>(pass.size()));
        Q_UNUSED(dummyResult);
        ok = false;
    }
    else
    {
        ok = verifyLegacySha256(password, storedHash);
    }

    wipe(pass);
    wipe(stored);
    return ok;
}

bool PasswordHasher::isArgon2Hash(const QString &hash)
{
    return hash.startsWith("$argon2");
}

bool PasswordHasher::verifyLegacySha256(const QString &password, const QString &storedHash)
{
    QByteArray pass = password.toUtf8();
    QByteArray computed = QCryptographicHash::hash(pass, QCryptographicHash::Sha256).toHex();
    QByteArray stored = storedHash.toLatin1();

    bool ok = false;

    if (computed.size() == stored.size())
    {
        unsigned char diff = 0;
        for (int i = 0; i < computed.size(); ++i)
        {
            diff |= static_cast<unsigned char>(computed[i]) ^ static_cast<unsigned char>(stored[i]);
        }
        ok = (diff == 0);
    }

    wipe(pass);
    wipe(computed);
    wipe(stored);
    return ok;
}



