#include "settings.h"

Settings::Settings(QString storage)
{
    dbconn=QSqlDatabase::addDatabase("QSQLITE");
    dbconn.setDatabaseName(storage);

    if (!dbconn.open())
    {
        lasterr=dbconn.lastError();
        return;
    }

}

QString Settings::GetLastError()
{
    return lasterr.text();
}

QVariant Settings::loadValue(QString name, QVariant defval)
{
    QSqlQuery query;

    query.prepare("select value from settings where name = :name");
    query.bindValue(":name", name);
    query.exec();

    lasterr = query.lastError();

    query.next();

    if (!query.isValid())
    {
        return defval;
    }

    return query.value(0);
}

void Settings::storeValue(QString name, QString val)
{
    QSqlQuery query;

    query.prepare("insert into settings (name, value) values (:name, :val)");
    query.bindValue(":addr", name);
    query.bindValue(":val", val);
    query.exec();

    lasterr = query.lastError();
}
