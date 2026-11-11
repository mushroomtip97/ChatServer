#include "serverconfig.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QDebug>

ServerConfig::ServerConfig(const QString &filePath)
    : m_filePath{filePath}
{
}

QString ServerConfig::resolvePath(const QString &path) const
{
    if (path.isEmpty())
    {
        return path;
    }

    const QFileInfo fi(path);
    if (fi.isAbsolute())
    {
        return path;
    }

    return QCoreApplication::applicationDirPath() + "/" + path;
}

bool ServerConfig::load()
{
    m_valid = false;
    m_error.clear();

    if (!QFileInfo::exists(m_filePath))
    {
        m_error = QString("Config file not found: %1").arg(m_filePath);
        qWarning() << m_error;
        return false;
    }

    QSettings settings(m_filePath, QSettings::IniFormat);

    // [server]
    settings.beginGroup("server");
    const uint rawPort = settings.value("port", kDefaultPort).toUInt();
    m_maxConnections = settings.value("max_connections", kDefaultMaxConnections).toInt();
    m_maxConnectionsPerIp = settings.value("max_connections_per_ip", kDefaultMaxConnectionsPerIp).toInt();
    m_authTimeoutSeconds = settings.value("auth_timeout_seconds", kDefaultAuthTimeoutSeconds).toInt();
    m_idleTimeoutSeconds = settings.value("idle_timeout_seconds", kDefaultIdleTimeoutSeconds).toInt();
    m_maxOutboundQueueBytes = settings.value("max_outbound_queue_bytes", kDefaultMaxOutboundQueueBytes).toLongLong();
    m_maxIncomingFrameSize = settings.value("max_incoming_frame_size", kDefaultMaxIncomingFrameSize).toULongLong();
    m_maxIncomingMessageSize = settings.value("max_incoming_message_size", kDefaultMaxIncomingMessageSize).toULongLong();
    settings.endGroup();

    if (rawPort == 0 || rawPort > 65535)
    {
        m_error = QString("Invalid server/port: %1").arg(rawPort);
        return false;
    }
    m_port = static_cast<quint16>(rawPort);

    if (m_maxConnections <= 0)
    {
        m_error = QString("Invalid server/max_connections: %1").arg(m_maxConnections);
        return false;
    }
    if (m_maxConnectionsPerIp <= 0)
    {
        m_error = QString("Invalid server/max_connections_per_ip: %1").arg(m_maxConnectionsPerIp);
        return false;
    }
    if (m_authTimeoutSeconds <= 0 || m_idleTimeoutSeconds <= 0)
    {
        m_error = QString("Invalid connection timeout: auth=%1 idle=%2").arg(m_authTimeoutSeconds).arg(m_idleTimeoutSeconds);
        return false;
    }

    // [database]
    settings.beginGroup("database");
    m_databasePath = resolvePath(settings.value("path", "chat_server.db").toString());
    settings.endGroup();

    // [tls]
    settings.beginGroup("tls");
    m_tlsEnabled = settings.value("enabled", true).toBool();
    if (m_tlsEnabled)
    {
        m_certPath = resolvePath(settings.value("cert_path", "certs/server.crt").toString());
        m_keyPath = resolvePath(settings.value("key_path", "certs/server.key").toString());

        if (!QFileInfo::exists(m_certPath))
        {
            m_error = QString("TLS enabled but certificate missing: %1").arg(m_certPath);
            return false;
        }
        if (!QFileInfo::exists(m_keyPath))
        {
            m_error = QString("TLS enabled but private key missing: %1").arg(m_keyPath);
            return false;
        }
        m_requirePrivateKeyPermissions =
            settings.value("require_private_key_permissions", true).toBool();
    }
    settings.endGroup();

    // [logging]
    settings.beginGroup("logging");
    m_logLevel = settings.value("level", "info").toString().toLower();
    m_logFilePath = resolvePath(settings.value("file", "chat_server.log").toString());
    settings.endGroup();

    // [health]
    settings.beginGroup("health");
    m_healthEnabled = settings.value("enabled", false).toBool();
    if (m_healthEnabled)
    {
        const uint rawHealthPort = settings.value("port", kDefaultHealthPort).toUInt();

        if (rawHealthPort == 0 || rawHealthPort > 65535)
        {
            m_error = QString("Invalid health/port: %1").arg(rawHealthPort);
            return false;
        }
        if (rawHealthPort == m_port)
        {
            m_error = QString("health/port (%1) must differ from server/port").arg(rawHealthPort);
            return false;
        }
        m_healthPort = static_cast<quint16>(rawHealthPort);
    }
    settings.endGroup();

    // [security]
    settings.beginGroup("security");
    m_authFailureLimit = settings.value("auth_failure_limit", kDefaultAuthFailureLimit).toInt();
    m_authIpFailureLimit = settings.value("auth_ip_failure_limit", kDefaultAuthIpFailureLimit).toInt();
    m_registrationRateLimitPerMinute = settings.value("registration_rate_limit_per_minute", kDefaultRegistrationRateLimitPerMinute).toInt();
    m_seedDemoUser = settings.value("seed_demo_user", false).toBool();
    settings.endGroup();

    if (m_authFailureLimit <= 0 || m_authIpFailureLimit <= 0 || m_registrationRateLimitPerMinute < 0)
    {
        m_error = QString("Invalid security limits: auth=%1 auth_ip=%2 registration=%3")
                      .arg(m_authFailureLimit)
                      .arg(m_authIpFailureLimit)
                      .arg(m_registrationRateLimitPerMinute);
        return false;
    }

    m_valid = true;
    return true;
}