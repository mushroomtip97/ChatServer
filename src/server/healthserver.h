#ifndef HEALTHSERVER_H
#define HEALTHSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QElapsedTimer>

class ChatServer;

class HealthServer : public QObject
{
    Q_OBJECT
public:
    explicit HealthServer(quint16 port, ChatServer *server, QObject *parent = nullptr);

    bool start();
    bool stop();
    bool isRunning() const { return m_running; }

private slots:
    void onNewConnection();

private:
    void handleRequest(QTcpSocket *socket, const QByteArray &requestData);
    QByteArray buildHealthResponse(bool *healthy) const;
    QByteArray buildMetricResponse();
    bool isDatabaseHealthy() const;

    QTcpServer *m_tcpServer;
    ChatServer *m_chatServer;
    quint16 m_port;
    bool m_running = false;
    QElapsedTimer m_uptime;
};

#endif // HEALTHSERVER_H