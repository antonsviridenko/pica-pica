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
#include "nodes.h"

Nodes::Nodes(QString storage)
{
	dbconn = QSqlDatabase::database();
	dbconn.setDatabaseName(storage);

	if (!dbconn.open())
	{
		lasterr = dbconn.lastError();
		return;
	}
//check if table nodes exists
}

void Nodes::Add(NodeRecord &n)
{
	QSqlQuery query;

	query.prepare("insert into nodes (address, port, last_active, inactive_count) values (:addr, :port, strftime('%s','now'), 0)");
	query.bindValue(":addr", n.address);
	query.bindValue(":port", n.port);
	query.exec();

	lasterr = query.lastError();
}

QList<Nodes::NodeRecord> Nodes::GetNodes()
{
	QList<NodeRecord> L;
	NodeRecord r;
	QSqlQuery query;

	query.setForwardOnly(true);
	query.exec("select address, port from nodes order by inactive_count asc, last_active desc");
	lasterr = query.lastError();

	if (lasterr.isValid())
		return L;

	while(query.next())
	{
		r.address = query.value(0).toString();
		r.port = query.value(1).toUInt();

		L << r;
	}

	return L;
}

void Nodes::MakeClean()
{

}

void Nodes::UpdateStatus(NodeRecord &n, bool alive)
{
	QSqlQuery query;

	if (alive)
		query.prepare("update nodes set last_active = strftime('%s','now'), inactive_count = 0 where address = :addr and port = :port");
	else
		query.prepare("update nodes set inactive_count = inactive_count + 1 where address = :addr and port = :port");

	query.bindValue(":addr", n.address);
	query.bindValue(":port", n.port);
	query.exec();

	lasterr = query.lastError();
}


