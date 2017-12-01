#ifndef SETTINGS_H
#define SETTINGS_H

#include <QtSql>
#include <QSqlDatabase>
#include <QVariant>

class Settings
{
public:
    Settings(QString storage);

    QVariant loadValue(QString name, QVariant defval);
    void storeValue(QString name, QString val);
	bool isEmpty();

    QString GetLastError();

private:
    QSqlDatabase dbconn;
    QSqlError lasterr;
};

#endif // SETTINGS_H
