/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
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
	query.bindValue(":name", name);
	query.bindValue(":val", val);
	query.exec();

	lasterr = query.lastError();
}
