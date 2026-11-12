#include <QtTest>

#include <QCryptographicHash>
#include <sodium.h>

#include "PasswordHasher.h"

class TestPasswordHasher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void hashProducesArgon2id();
    void emptyPasswordHashIsEmpty();
    void verifyAcceptsCorrectPassword();
    void verifyRejectsWrongPassword();
    void verifyEmptyStoredHashFails();
    void verifyMalformedStoredHashFails();
    void verifyLegacySha256StillWorks();
};

void TestPasswordHasher::initTestCase()
{
    if (sodium_init() < 0)
    {
        QFAIL("libsodium initialization failed");
    }
}

void TestPasswordHasher::hashProducesArgon2id()
{
    const QString hash = PasswordHasher::hash("correct horse battery staple");
    QVERIFY(hash.startsWith("$argon2id$"));
}

void TestPasswordHasher::emptyPasswordHashIsEmpty()
{
    QVERIFY(PasswordHasher::hash(QString()).isEmpty());
}

void TestPasswordHasher::verifyAcceptsCorrectPassword()
{
    const QString password = "s3cret-password";
    const QString hash = PasswordHasher::hash(password);

    QVERIFY(PasswordHasher::verify(password, hash));
}

void TestPasswordHasher::verifyRejectsWrongPassword()
{
    const QString password = "s3cret-password";
    const QString hash = PasswordHasher::hash(password);

    QVERIFY(!PasswordHasher::verify("wrong-password", hash));
}

void TestPasswordHasher::verifyEmptyStoredHashFails()
{
    // Must fail, but must still burn Argon2 time internally.
    QVERIFY(!PasswordHasher::verify("some-password", QString()));
}

void TestPasswordHasher::verifyMalformedStoredHashFails()
{
    QVERIFY(!PasswordHasher::verify("some-password", "not-a-valid-hash"));
}

void TestPasswordHasher::verifyLegacySha256StillWorks()
{
    const QString password = "legacy-password";
    const QString stored = QString::fromLatin1(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());

    QVERIFY(PasswordHasher::verifyLegacySha256(password, stored));
    QVERIFY(!PasswordHasher::verifyLegacySha256("wrong-password", stored));
}

QTEST_APPLESS_MAIN(TestPasswordHasher)
#include "test_passwordhasher.moc"
