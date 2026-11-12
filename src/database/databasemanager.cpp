#include "databasemanager.h"
#include <QSql>
#include <QSqlError>
#include <QSqlQuery>
#include <QCryptographicHash>


DatabaseManager::DatabaseManager()
    :m_connectionName{"chat-server-connection"}, m_initialized{false}
{}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::initialize(const QString &dbPath, bool seedDemoUser)
{
    if(m_initialized)
    {
        qWarning() << "Database already initilized";
        return true;
    }

    m_seedDemoUser = seedDemoUser;
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if(!m_db.open())
    {
        qCritical() << "Failed to open database" << m_db.lastError().text();
        return false;
    }

    // Enable foreign keys and WAL mode for better concurrency
    QSqlQuery query(m_db);

    if (!query.exec("PRAGMA foreign_keys = ON"))
    {
        qWarning() << "Failed to enable foreign keys:" << query.lastError().text();
    }

    if (!query.exec("PRAGMA journal_mode = WAL"))
    {
        qWarning() << "Failed to set WAL mode:" << query.lastError().text();
    }

    if (!query.exec("PRAGMA synchronous = NORMAL"))
    {
        qWarning() << "Failed to set synchronous mode:" << query.lastError().text();
    }
    if (!query.exec("PRAGMA busy_timeout = 5000"))
    {
        qWarning() << "Failed to set busy timeout:" << query.lastError().text();
    }



    // Tables and indexes
    if (!createTables())
    {
        qCritical() << "Failed to create tables";
        close();
        return false;
    }


    if(!migrateSchema())
    {
        qCritical() << "Migration has failed";
        close();
        return false;
    }
    if(!createIndexes())
    {
        qCritical() << "Failed to create indexes";
        close();
        return false;
    }


    // Seed default data
    if (!seedDefaultData())
    {
        qCritical() << "Failed to seed default data";
        close();
        return false;
    }


    m_initialized = true;
    qInfo() << "Database initialized successfully at:" << dbPath;
    return true;

}

QSqlDatabase DatabaseManager::database() const
{
    return m_db;
}

void DatabaseManager::close()
{
    if (m_db.isOpen())
    {
        // Collapses WAL back into the main DB and truncates the WAL file as well.
        QSqlQuery querry{m_db};
        querry.prepare("PRAGMA wal_checkpoint(TRUNCATE)");

        if(!querry.exec())
        {
            qWarning() << "WAL checkpoint failed" << querry.lastError().text();
        }
        m_db.close();
    }

    if (QSqlDatabase::contains(m_connectionName))
    {
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    m_initialized = false;
}

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);

    // Users
    QString createUsersTable = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";

    if (!query.exec(createUsersTable))
    {
        qCritical() << "Failed to create users table:" << query.lastError().text();
        return false;
    }

    // Rooms
    QString createRoomsTable = R"(
        CREATE TABLE IF NOT EXISTS rooms (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";

    if (!query.exec(createRoomsTable))
    {
        qCritical() << "Failed to create rooms table:" << query.lastError().text();
        return false;
    }

    // Room members
    QString createRoomMembersTable = R"(
        CREATE TABLE IF NOT EXISTS room_members (
            room_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (room_id, user_id),
            FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE,
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        )
    )";

    if (!query.exec(createRoomMembersTable))
    {
        qCritical() << "Failed to create room_members table:" << query.lastError().text();
        return false;
    }

    // Messages
    QString createMessagesTable = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            room_id INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE,
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        )
    )";

    QString createPrivateMessageTable = R"(
        CREATE TABLE IF NOT EXISTS private_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sender_id INTEGER NOT NULL,
            recipient_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (sender_id) REFERENCES users(id) ON DELETE CASCADE,
            FOREIGN KEY (recipient_id) REFERENCES users(id) ON DELETE CASCADE
        )
    )";

    if (!query.exec(createMessagesTable))
    {
        qCritical() << "Failed to create messages table:" << query.lastError().text();
        return false;
    }

    if(!query.exec(createPrivateMessageTable))
    {
        qCritical() << "Failed to create private message table" << query.lastError().text();
        return false;
    }

    qInfo() << "All tables created successfully";
    return true;
}

bool DatabaseManager::createIndexes()
{
    QSqlQuery query(m_db);

    QString createMessageIndex = R"(CREATE INDEX IF NOT EXISTS idx_messages_room_created ON messages(room_id, created_at))";

    if (!query.exec(createMessageIndex))
    {
        qCritical() << "Failed to create messages index:" << query.lastError().text();
        return false;
    }

    QString createPrivateMessageSenderIndex = R"(CREATE INDEX IF NOT EXISTS idx_pm_conversation ON private_messages(sender_id, recipient_id, created_at))";
    if(!query.exec(createPrivateMessageSenderIndex))
    {
        qCritical() << "Failed to create private message index: " << query.lastError().text();
        return false;
    }

    QString createPrivateMessageRecipientIndex = R"(CREATE INDEX IF NOT EXISTS idx_pm_recipient ON private_messages(recipient_id, created_at))";
    if(!query.exec(createPrivateMessageRecipientIndex))
    {
        qCritical() << "Failed to create private recipient message index: " << query.lastError().text();
        return false;
    }

    QString createUsernameIndex = R"(CREATE INDEX IF NOT EXISTS idx_users_username ON users(username))";

    if (!query.exec(createUsernameIndex))
    {
        qCritical() << "Failed to create users index:" << query.lastError().text();
        return false;
    }

    // Index for room name lookups
    QString createRoomNameIndex = R"(CREATE INDEX IF NOT EXISTS idx_rooms_name ON rooms(name))";

    if (!query.exec(createRoomNameIndex))
    {
        qCritical() << "Failed to create rooms index:" << query.lastError().text();
        return false;
    }

    QString createPmUnreadIndex = R"(CREATE INDEX IF NOT EXISTS idx_pm_unread ON private_messages(recipient_id, read_at))";
    if (!query.exec(createPmUnreadIndex))
    {
        qCritical() << "Failed to create unread index:" << query.lastError().text();
        return false;
    }

    qInfo() << "All indexes created successfully";
    return true;
}

bool DatabaseManager::seedDefaultData()
{
    QSqlQuery query(m_db);


    if (!query.exec("SELECT COUNT(*) FROM rooms"))
    {
        qCritical() << "Failed to check rooms table:" << query.lastError().text();
        return false;
    }

    query.next();
    int roomCount = query.value(0).toInt();

    if (roomCount == 0)
    {

        QStringList defaultRooms = {"general", "random", "tech", "support"};

        for (const QString &roomName : defaultRooms)
        {
            QSqlQuery insertQuery(m_db);
            insertQuery.prepare("INSERT INTO rooms (name) VALUES (?)");
            insertQuery.addBindValue(roomName);

            if (!insertQuery.exec())
            {
                qWarning() << "Failed to create default room" << roomName << ":" << insertQuery.lastError().text();
            }
            else
            {
                qInfo() << "Created default room:" << roomName;
            }
        }
    }

    if (!query.exec("SELECT COUNT(*) FROM users"))
    {
        qCritical() << "Failed to check users table:" << query.lastError().text();
        return false;
    }

    query.next();
    int userCount = query.value(0).toInt();

    if (userCount == 0 && m_seedDemoUser)
    {
        // Seeded with legacy SHA-256. Users with legacy hashes are automatically
        // upgraded to Argon2id on their next successful login (see AuthManager).
        QString passwordHash = QString::fromUtf8(QCryptographicHash::hash("demo123", QCryptographicHash::Sha256).toHex());

        QSqlQuery insertQuery(m_db);
        insertQuery.prepare("INSERT INTO users (username, password_hash) VALUES (?, ?)");
        insertQuery.addBindValue("demo");
        insertQuery.addBindValue(passwordHash);

        if (!insertQuery.exec())
        {
            qWarning() << "Failed to create demo user:" << insertQuery.lastError().text();
        }
        else
        {
            qInfo() << "Created dev-only demo user: demo";

            // Add demo user to general room
            int userId = insertQuery.lastInsertId().toInt();
            QSqlQuery memberQuery(m_db);
            memberQuery.prepare("INSERT INTO room_members (room_id, user_id) VALUES (1, ?)");
            memberQuery.addBindValue(userId);

            if (!memberQuery.exec())
            {
                qWarning() << "Failed to add demo user to general room:" << memberQuery.lastError().text();
            }
        }
    }

    qInfo() << "Default data seeded successfully";
    return true;
}

bool DatabaseManager::columnExists(const QString &table, const QString &column)
{
    QSqlQuery querry{m_db};
    querry.prepare(QString("PRAGMA table_info(%1)").arg(table));

    if(!querry.exec())
    {
        qWarning() << querry.lastError().text();
        return false;
    }
    while(querry.next())
    {
        if(querry.value("name").toString() == column)
        {
            return true;
        }
    }

    return false;
}

bool DatabaseManager::addColumnIfMissing(const QString &table, const QString &column, const QString &type)
{
    if (columnExists(table, column))
    {
        return true;
    }

    QSqlQuery q{m_db};
    q.prepare(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, type));

    if (!q.exec())
    {
        qCritical() << "Migration failed (" << column << "):" << q.lastError().text();
        return false;
    }

    qInfo() << "Migration: added" << column << "to" << table;
    return true;
}

bool DatabaseManager::migrateSchema()
{
    if (!addColumnIfMissing("private_messages", "read_at", "TIMESTAMP"))
    {
        return false;
    }
    if (!addColumnIfMissing("messages", "edited_at", "TIMESTAMP"))
    {
        return false;
    }
    if (!addColumnIfMissing("messages", "deleted_at", "TIMESTAMP"))
    {
        return false;
    }
    return true;
}

