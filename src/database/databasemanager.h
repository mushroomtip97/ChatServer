#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>
#include <QString>
#include <memory>

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    bool initialize(const QString &dbPath = "chat_server.db", bool seedDemoUser = false);
    QSqlDatabase database()const;
    void close();

    QSqlDatabase db() const;

private:
    bool createTables();
    bool createIndexes();
    bool seedDefaultData();
    bool executeSqlFile(const QString &filePath);
    bool columnExists(const QString &table, const QString &column);
    bool addColumnIfMissing(const QString &table, const QString &column, const QString &type);
    bool migrateSchema();


    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_seedDemoUser = false;
    bool m_initialized;
};

#endif // DATABASEMANAGER_H
