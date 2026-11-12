#include "filelogger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <cstdio>

namespace
{
    QtMessageHandler s_previousHandler = nullptr;

    void fileLoggerMessageHandler(QtMsgType type,const QMessageLogContext &ctx, const QString &msg)
    {
        FileLogger::instance().write(type, ctx, msg);

        // Chains to whatever handler was installed before us, so the console
        if (s_previousHandler)
        {
            s_previousHandler(type, ctx, msg);
        }
    }
}


FileLogger &FileLogger::instance()
{
    static FileLogger logger;
    return logger;
}


QtMsgType FileLogger::parseLevel(const QString &lvl)
{
    const QString l = lvl.toLower();

    if(l == "debug")
    {
        return QtDebugMsg;
    }
    if(l == "info")
    {
        return QtInfoMsg;
    }
    if(l == "warning")
    {
        return QtWarningMsg;
    }
    if(l == "critical")
    {
        return QtCriticalMsg;
    }
    if(l == "fatal")
    {
        return QtFatalMsg;
    }

    return QtInfoMsg;
}

QString FileLogger::typeToString(QtMsgType type)
{
    switch(type)
    {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "CRIT";
    case QtFatalMsg:
        return "FATAL";
    }

    return "?????";
}

void FileLogger::reopen()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    m_file.setFileName(m_filePath);

    if(!m_file.open(QIODevice::Append | QIODevice::Text))
    {
        std::fprintf(stderr, "FileLogger:: cannot open %s : %s\n", qPrintable(m_filePath), qPrintable(m_file.errorString()));
        m_bytesWritten = 0;
        return;
    }

    m_file.seek(m_file.size());
    m_bytesWritten = m_file.pos();

}



void FileLogger::install(const QString &filePath, const QString &minLvl)
{
    QMutexLocker locker{&m_mutex};
    if(m_installed)
    {
        return;
    }

    m_filePath = filePath;
    m_minLevel = parseLevel(minLvl);
    reopen();

    s_previousHandler = qInstallMessageHandler(fileLoggerMessageHandler);
    m_installed = true;
}

void FileLogger::uninstall()
{
    QMutexLocker locker{&m_mutex};
    if(!m_installed)
    {
        return;
    }

    qInstallMessageHandler(s_previousHandler);
    s_previousHandler = nullptr;

    if(m_file.isOpen())
    {
        flushBuffer();
        m_file.close();
    }

    m_installed = false;
}



void FileLogger::flushBuffer()
{
    if (!m_file.isOpen() || m_buffer.isEmpty())
    {
        return;
    }

    m_file.write(m_buffer);
    m_file.flush();
    m_buffer.clear();
}

void FileLogger::rotateIfNeeded()
{
    if(!m_file.isOpen())
    {
        return;
    }
    if(m_bytesWritten < m_maxSize)
    {
        return;
    }

    flushBuffer();
    m_file.close();

    for(int i = m_maxFiles - 1; i >= 1; i--)
    {
        const QString newer = QString("%1.%2").arg(m_filePath).arg(i);
        const QString older = QString("%1.%2").arg(m_filePath).arg(i + 1);

        if(QFile::exists(older))
        {
            QFile::remove(older);
        }
        if(QFile::exists(newer))
        {
            QFile::rename(newer, older);
        }
    }

    const QString first = m_filePath + ".1";
    if (QFile::exists(first))
    {
        QFile::remove(first);
    }
    QFile::rename(m_filePath, first);

    reopen();
}

void FileLogger::write(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if(type < m_minLevel)
    {
        return;
    }

    QMutexLocker locker{&m_mutex};

    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    const QString lvl = typeToString(type);

    const QString category = (ctx.category && *ctx.category && qstrcmp(ctx.category, "default") != 0) ? QString("[%1] ").arg(ctx.category) : QString();
    const QString line = QString("[%1] [%2] %3%4\n").arg(ts, lvl, category, msg);

    if (m_file.isOpen())
    {
        const QByteArray bts = line.toUtf8();
        m_buffer.append(bts);
        m_bytesWritten += bts.size();

        // Flush on warnings/critical messages or once the buffer is full.
        if (type >= QtWarningMsg || m_buffer.size() >= kBufferFlushSize)
        {
            flushBuffer();
        }

        if(m_bytesWritten >= m_maxSize)
        {
            rotateIfNeeded();
        }
    }
}



