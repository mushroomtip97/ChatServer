#include "healthserver.h"
#include "chatserver.h"

#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace {
constexpr qint64 kMaxRequestBytes = 16 * 1024;
constexpr int kRequestTimeoutMs = 10 * 1000;
} // namespace

HealthServer::HealthServer(quint16 port, ChatServer *server, QObject *parent)
    : QObject{parent}, m_tcpServer{nullptr}, m_chatServer{server}, m_port{port}
{
}

bool HealthServer::start()
{
    if (m_running)
    {
        return true;
    }

    m_tcpServer = new QTcpServer{this};

    if (!m_tcpServer->listen(QHostAddress::LocalHost, m_port))
    {
        qCritical() << "Health server failed to listen on port: " << m_port << ":" << m_tcpServer->errorString();
        delete m_tcpServer;
        m_tcpServer = nullptr;
        return false;
    }

    connect(m_tcpServer, &QTcpServer::newConnection, this, &HealthServer::onNewConnection);
    m_uptime.start();
    m_running = true;
    qInfo() << "Health/metrics server listening on port: " << m_port;

    return true;
}

bool HealthServer::stop()
{
    if (!m_running)
    {
        return false;
    }

    if (m_tcpServer)
    {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    m_uptime.invalidate();
    m_running = false;
    return true;
}

void HealthServer::onNewConnection()
{
    QTcpSocket *socket = m_tcpServer->nextPendingConnection();
    if (!socket)
    {
        return;
    }

    QTimer::singleShot(kRequestTimeoutMs, socket, [socket]() {
        if (socket && socket->state() == QAbstractSocket::ConnectedState)
        {
            socket->abort();
        }
    });

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        const QByteArray request = socket->readAll();
        if (request.size() > kMaxRequestBytes)
        {
            socket->abort();
            return;
        }
        if (!request.contains("\r\n\r\n"))
        {
            return;
        }
        handleRequest(socket, request);
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
}

void HealthServer::handleRequest(QTcpSocket *socket, const QByteArray &requestData)
{
    const int firstLineEnd = requestData.indexOf("\r\n");
    if (firstLineEnd <= 0)
    {
        socket->abort();
        return;
    }

    const QByteArray firstLine = requestData.left(firstLineEnd);
    const QList<QByteArray> parts = firstLine.split(' ');

    QByteArray path = "/";
    if (parts.size() >= 2)
    {
        path = parts.at(1);
    }

    QByteArray body;
    QByteArray statusLine;

    if (path == "/health" || path == "/healthz" || path == "/readyz")
    {
        bool healthy = false;
        body = buildHealthResponse(&healthy);
        statusLine = healthy ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 503 Service Unavailable\r\n";
    }
    else if (path == "/metrics")
    {
        body = buildMetricResponse();
        statusLine = "HTTP/1.1 200 OK\r\n";
    }
    else
    {
        body = "{\"error\":\"not found\"}";
        statusLine = "HTTP/1.1 404 Not Found\r\n";
    }

    const QByteArray contentType = (path == "/metrics") ? "text/plain; version=0.0.4; charset=utf-8" : "application/json; charset=utf-8";

    QByteArray response;
    response += statusLine;
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

bool HealthServer::isDatabaseHealthy() const
{
    return m_chatServer && m_chatServer->isDatabaseHealthy();
}

QByteArray HealthServer::buildHealthResponse(bool *healthy) const
{
    const bool dbOk = isDatabaseHealthy();
    if (healthy)
    {
        *healthy = dbOk;
    }

    QJsonObject obj;
    obj["status"] = dbOk ? "ok" : "degraded";
    obj["uptime_seconds"] = m_uptime.isValid() ? m_uptime.elapsed() / 1000 : 0;
    obj["clients"] = m_chatServer ? m_chatServer->connectedClientCount() : 0;
    obj["db"] = dbOk ? "ok" : "error";

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray HealthServer::buildMetricResponse()
{
    const qint64 uptimeSec = m_uptime.isValid() ? m_uptime.elapsed() / 1000 : 0;

    QByteArray out;
    out += "# HELP chat_uptime_seconds Server uptime in seconds\n";
    out += "# TYPE chat_uptime_seconds gauge\n";
    out += "chat_uptime_seconds " + QByteArray::number(uptimeSec) + "\n\n";

    out += "# HELP chat_connected_clients Currently connected clients\n";
    out += "# TYPE chat_connected_clients gauge\n";
    out += "chat_connected_clients " + QByteArray::number(m_chatServer->connectedClientCount()) + "\n\n";

    out += "# HELP chat_messages_sent_total Total room messages sent\n";
    out += "# TYPE chat_messages_sent_total counter\n";
    out += "chat_messages_sent_total " + QByteArray::number(m_chatServer->messagesSent()) + "\n\n";

    out += "# HELP chat_private_messages_sent_total Total PMs sent\n";
    out += "# TYPE chat_private_messages_sent_total counter\n";
    out += "chat_private_messages_sent_total " + QByteArray::number(m_chatServer->privateMessagesSent()) + "\n\n";

    out += "# HELP chat_auth_failures_total Total authentication failures\n";
    out += "# TYPE chat_auth_failures_total counter\n";
    out += "chat_auth_failures_total " + QByteArray::number(m_chatServer->authFailures()) + "\n\n";

    out += "# HELP chat_registrations_total Total successful registrations\n";
    out += "# TYPE chat_registrations_total counter\n";
    out += "chat_registrations_total " + QByteArray::number(m_chatServer->registrations()) + "\n";

    return out;
}