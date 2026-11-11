#ifndef FILELOGGER_H
#define FILELOGGER_H

#include <QFile>
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QFileInfo>
#include <QtGlobal>

class FileLogger
{
public:
    static FileLogger &instance();


    void install(const QString &filePath, const QString &minLvl);
    void uninstall();
    void write(QtMsgType type, const QMessageLogContext &ctx, const QString &msg);


private:

    FileLogger() = default;
    FileLogger(const FileLogger &) = delete;
    FileLogger &operator=(const FileLogger &) = delete;

    static QtMsgType parseLevel(const QString &lvl);
    static QString typeToString(QtMsgType type);


    void reopen();
    void flushBuffer();
    void rotateIfNeeded();


    QFile    m_file;
    QString  m_filePath;
    qint64   m_maxSize  = 10 * 1024 * 1024;   // 10 MB
    int      m_maxFiles = 5;
    qint64 m_bytesWritten = 0; //In case when QFile::size() keeps returnin stale on Windows so size must be tracked yourself
    QByteArray m_buffer;
    static constexpr qint64 kBufferFlushSize = 64 * 1024;
    QtMsgType m_minLevel = QtInfoMsg;

    QMutex m_mutex;
    bool m_installed = false;
};

#endif // FILELOGGER_H
