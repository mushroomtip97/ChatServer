#include "TransactionGuard.h"

bool TransactionGuard::active() const
{
    return m_active;
}

TransactionGuard::TransactionGuard(QSqlDatabase db)
    :m_db{db}, m_active{m_db.transaction()}, m_committed{false}
{}
TransactionGuard::~TransactionGuard()
{
    if(m_active && !m_committed)
    {
        m_db.rollback();
    }
}

bool TransactionGuard::commit()
{
    if(!m_active)
    {
        return false;
    }
    m_committed = m_db.commit();
    if(!m_committed)
    {
        return false;
    }
    m_active = false;

    return m_committed;
}

