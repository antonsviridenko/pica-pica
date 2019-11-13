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
