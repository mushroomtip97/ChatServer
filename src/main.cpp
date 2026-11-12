
#include "databasemanager.h"
#include "messageprotocol.h"
#include "presencemanager.h"
#include "clientconnection.h"

#include <QCoreApplication>
#include <QDir>
#include "config/serverconfig.h"
#include "chatserver.h"
#include "logging/filelogger.h"
#include <sodium.h>




#ifdef Q_OS_WIN
#include <windows.h>

static BOOL WINAPI consoleCtrlHandler(DWORD type)
{
    switch(type)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:

    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
        return true;
    default:
        return false;
    }
}
#endif

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("chat-server");
    app.setApplicationVersion("1.0.0");

#ifdef Q_OS_WIN
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE))
    {
        qWarning() << "Failed to install console control handler";
    }
#endif

    const QString confPath = QCoreApplication::applicationDirPath() + "/config/server.conf";
    ServerConfig config(confPath);
    if (!config.load())
    {
        qCritical() << "Configuration error:" << config.lastError();
        return 1;
    }

    FileLogger::instance().install(config.logFilePath(), config.logLevel());

    qInfo() << "Chat Server v1.0.0 starting...";
    qInfo() << "Logging to" << config.logFilePath() << "at level" << config.logLevel();
    qInfo() << "Config loaded: port=" << config.port()
            << "db=" << config.databasePath()
            << "tls=" << config.tlsEnabled()
            << "health=" << config.healthEnabled()
            << "healthPort=" << config.healthPort();

    if (sodium_init() < 0)
    {
        qCritical() << "libsodium failed";
        return 1;
    }
    qInfo() << "libsodium" << sodium_version_string() << "ready for use";

    ChatServer server(config);
    if (!server.start())
    {
        qCritical() << "Failed to start chat server";
        return 1;
    }

    qInfo() << "Chat server is running. Press Ctrl+C to stop.";
    int rc = app.exec();

    FileLogger::instance().uninstall();
    return rc;
}


