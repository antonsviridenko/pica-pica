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
#ifndef NODES_H
#define NODES_H

#include <QList>
#include <QString>
#include <QtSql>
#include <QSqlDatabase>

class Nodes
{
public:
	struct NodeRecord
	{
		QString address;
		quint16 port;
	};

	// connectionName selects which QSqlDatabase connection to use (see
	// QSqlDatabase::database()). A connection can only be used from the
	// thread that created it, so callers running on a thread other than
	// the GUI thread must pass the name of a connection opened on their
	// own thread. Empty (the default) uses Qt's default connection.
	Nodes(QString storage, QString connectionName = QString());
	void Add(NodeRecord &n);
	void Delete(NodeRecord &n);
	QList<NodeRecord> GetNodes();
	void MakeClean();//remove old inactive node records
	void UpdateStatus(NodeRecord &n, bool alive);

private:
	QSqlDatabase dbconn;
	QSqlError lasterr;

};

#endif // NODES_H
