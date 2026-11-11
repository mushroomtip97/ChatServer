#ifndef TRANSACTIONGUARD_H
#define TRANSACTIONGUARD_H

#include <QSqlDatabase>

class TransactionGuard
{
private:
    QSqlDatabase m_db;
    bool m_active;
    bool m_committed;
public:
    TransactionGuard(const TransactionGuard &) = default;
    TransactionGuard(TransactionGuard &&) = delete;

    TransactionGuard &operator=(const TransactionGuard &) = delete;
    TransactionGuard &operator=(TransactionGuard &&) = delete;

    explicit TransactionGuard(QSqlDatabase db);
    ~TransactionGuard();

    bool commit();
    bool active() const;
};

#endif // TRANSACTIONGUARD_H
