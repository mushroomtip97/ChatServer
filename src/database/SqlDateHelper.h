#ifndef SQLDATEHELPER_H
#define SQLDATEHELPER_H

#include <QTimeZone>
#include <QDateTime>

namespace SqlDateHelper
{
    static QDateTime parseInternal(const QVariant &v)
    {
        if(!v.isValid() || v.isNull())
        {
            return QDateTime();
        }

        QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODate);
        if(!dt.isValid())
        {
            dt = v.toDateTime();
        }
        if(dt.isValid())
        {
            QTimeZone zone = QTimeZone::utc();
            dt.setTimeZone(zone);
        }
        return dt;
    }

    QDateTime parseNullable(const QVariant &value)
    {
        return parseInternal(value);
    }

    QDateTime parseOrNow(const QVariant &value)
    {
        QDateTime dt = parseInternal(value);
        return dt.isValid() ? dt : QDateTime::currentDateTimeUtc();
    }

}



#endif // SQLDATEHELPER_H
