#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>
#include <QtGlobal>

class ServerConfig
{
public:
    explicit ServerConfig(const QString &filePath);

    bool load();

    bool isValid() const { return m_valid; }
    QString lastError() const { return m_error; }

    quint16 port() const { return m_port; }
    int maxConnections() const { return m_maxConnections; }
    int maxConnectionsPerIp() const { return m_maxConnectionsPerIp; }
    int authTimeoutSeconds() const { return m_authTimeoutSeconds; }
    int idleTimeoutSeconds() const { return m_idleTimeoutSeconds; }
    qint64 maxOutboundQueueBytes() const { return m_maxOutboundQueueBytes; }
    quint64 maxIncomingFrameSize() const { return m_maxIncomingFrameSize; }
    quint64 maxIncomingMessageSize() const { return m_maxIncomingMessageSize; }

    QString databasePath() const { return m_databasePath; }
    bool tlsEnabled() const { return m_tlsEnabled; }
    bool requirePrivateKeyPermissions() const { return m_requirePrivateKeyPermissions; }
    QString certPath() const { return m_certPath; }
    QString keyPath() const { return m_keyPath; }
    QString logLevel() const { return m_logLevel; }
    QString logFilePath() const { return m_logFilePath; }

    bool healthEnabled() const { return m_healthEnabled; }
    quint16 healthPort() const { return m_healthPort; }

    int authFailureLimit() const { return m_authFailureLimit; }
    int authIpFailureLimit() const { return m_authIpFailureLimit; }
    int registrationRateLimitPerMinute() const { return m_registrationRateLimitPerMinute; }
    bool seedDemoUser() const { return m_seedDemoUser; }

    // Defaults
    static constexpr quint16 kDefaultPort = 8080;
    static constexpr int kDefaultMaxConnections = 100;
    static constexpr int kDefaultMaxConnectionsPerIp = 100;
    static constexpr int kDefaultAuthTimeoutSeconds = 30;
    static constexpr int kDefaultIdleTimeoutSeconds = 300;
    static constexpr qint64 kDefaultMaxOutboundQueueBytes = 1024 * 1024;
    static constexpr quint64 kDefaultMaxIncomingFrameSize = 4 * 1024 * 1024;
    static constexpr quint64 kDefaultMaxIncomingMessageSize = 1024 * 1024;
    static constexpr quint16 kDefaultHealthPort = 8081;
    static constexpr int kDefaultAuthFailureLimit = 10;
    static constexpr int kDefaultAuthIpFailureLimit = 30;
    static constexpr int kDefaultRegistrationRateLimitPerMinute = 0; // 0 = unlimited

private:
    QString resolvePath(const QString &path) const;

    QString m_filePath;
    QString m_error;
    bool m_valid = false;

    quint16 m_port = kDefaultPort;
    int m_maxConnections = kDefaultMaxConnections;
    int m_maxConnectionsPerIp = kDefaultMaxConnectionsPerIp;
    int m_authTimeoutSeconds = kDefaultAuthTimeoutSeconds;
    int m_idleTimeoutSeconds = kDefaultIdleTimeoutSeconds;
    qint64 m_maxOutboundQueueBytes = kDefaultMaxOutboundQueueBytes;
    quint64 m_maxIncomingFrameSize = kDefaultMaxIncomingFrameSize;
    quint64 m_maxIncomingMessageSize = kDefaultMaxIncomingMessageSize;

    QString m_databasePath = "chat_server.db";
    bool m_tlsEnabled = true;
    bool m_requirePrivateKeyPermissions = true;
    QString m_certPath = "certs/server.crt";
    QString m_keyPath = "certs/server.key";
    QString m_logLevel = "info";
    QString m_logFilePath = "chat_server.log";

    bool m_healthEnabled = false;
    quint16 m_healthPort = kDefaultHealthPort;

    int m_authFailureLimit = kDefaultAuthFailureLimit;
    int m_authIpFailureLimit = kDefaultAuthIpFailureLimit;
    int m_registrationRateLimitPerMinute = kDefaultRegistrationRateLimitPerMinute;
    bool m_seedDemoUser = false;
};

#endif // SERVERCONFIG_H