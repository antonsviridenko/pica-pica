#include "settings.h"

Settings::Settings(QString storage)
{
	dbconn = QSqlDatabase::database();
	dbconn.setDatabaseName(storage);

	if (!dbconn.open())
	{
		lasterr = dbconn.lastError();
		return;
	}

}

QString Settings::GetLastError()
{
	return lasterr.text();
}

bool Settings::isEmpty()
{
	QSqlQuery query;

	query.exec("select count(*) from settings");

	lasterr = query.lastError();

	query.next();

	if (query.isValid() && query.value(0).toInt() == 0)
	{
		return true;
	}

	return false;
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
