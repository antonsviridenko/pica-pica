/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

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

	Nodes(QString storage);
	void Add(NodeRecord &n);
	QList<NodeRecord> GetNodes();
	void MakeClean();//remove old inactive node records
	void UpdateStatus(NodeRecord &n, bool alive);

private:
	QSqlDatabase dbconn;
	QSqlError lasterr;

};

#endif // NODES_H
