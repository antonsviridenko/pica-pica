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
#include "history.h"
#include <QDebug>

History::History(QString storage, QByteArray my_id)
	: me_(my_id)
{
	dbconn = QSqlDatabase::database();
	dbconn.setDatabaseName(storage);

	if (!dbconn.open())
	{
		lasterr = dbconn.lastError();
		return;
	}

	QSqlQuery query;

	query.exec("PRAGMA foreign_keys=ON;");

	//check result, if foreign keys support is on
}

void History::Add(QByteArray peer_id, QString message, bool is_me)
{
	QSqlQuery query;

	query.prepare(
	    "insert into \"history\" (\"contact_id\", \"account_id\", \"is_me\", \"is_delivered\", \"message\", \"id\", \"timestamp\") \
                values (:contact_id, :account_id, :is_me, :is_delivered, :message, null, strftime('%s','now'));"
	);

	query.bindValue(":contact_id", peer_id);
	query.bindValue(":account_id", me_);

	if (is_me)
	{
		query.bindValue(":is_me", 1);
		query.bindValue(":is_delivered", 0);
	}
	else
	{
		query.bindValue(":is_me", 0);
		query.bindValue(":is_delivered", 1);
	}

	query.bindValue(":message", message);

	query.exec();

	lasterr = query.lastError();
}

void History::SetDelivered(QByteArray peer_id)
{
	QSqlQuery query;

	query.prepare("update history set is_delivered=1 where id in \
                  (select id from history where \
                   is_delivered=0  and is_me=1 and contact_id=:peer_id and account_id=:me order by id asc limit 1)"
	             );

	query.bindValue(":peer_id", peer_id);
	query.bindValue(":me", me_);

	query.exec();
	lasterr = query.lastError();
}

QList<History::HistoryRecord> History::GetMessages(QByteArray peer_id, QString keyword)
{
	QList<HistoryRecord> L;
	HistoryRecord r;
	QSqlQuery query;

	query.setForwardOnly(true);
	query.prepare("select contact_id, is_me, is_delivered, timestamp, message from history \
               where contact_id=:peer_id and account_id=:me and message like :keyword order by id"
	             );

	query.bindValue(":peer_id", peer_id);
	query.bindValue(":me", me_);
	query.bindValue(":keyword", QString("%%1%").arg(keyword));
	query.exec();

	lasterr = query.lastError();

	if (lasterr.isValid())
		return L;

	while(query.next())
	{
		r.peer_id =      query.value(0).toByteArray();
		r.is_me =        query.value(1).toBool();
		r.is_delivered = query.value(2).toBool();
		r.timestamp =    query.value(3).toUInt();
		r.message =      query.value(4).toString();

		L << r;
	}

	return L;
}

QList<History::HistoryRecord> History::GetMessages(QByteArray peer_id, quint32 start_timestamp, quint32 end_timestamp)
{
	QList<HistoryRecord> L;
	HistoryRecord r;
	QSqlQuery query;

	query.setForwardOnly(true);
	query.prepare("select contact_id, is_me, is_delivered, timestamp, message from history \
               where contact_id=:peer_id and account_id=:me and timestamp >= :start and timestamp <=:end order by id"
	             );

	query.bindValue(":peer_id", peer_id);
	query.bindValue(":me", me_);
	query.bindValue(":start", start_timestamp);
	query.bindValue(":end", end_timestamp);
	query.exec();

	lasterr = query.lastError();

	if (lasterr.isValid())
		return L;

	while(query.next())
	{
		r.peer_id =      query.value(0).toByteArray();
		r.is_me =        query.value(1).toBool();
		r.is_delivered = query.value(2).toBool();
		r.timestamp =    query.value(3).toUInt();
		r.message =      query.value(4).toString();

		L << r;
	}

	return L;
}

QMap<QByteArray, QList<QString> >  History::GetUndeliveredMessages()
{
	QMap<QByteArray, QList<QString> > M;
	QSqlQuery query;

	query.setForwardOnly(true);
	query.prepare("select contact_id, message from history \
               where account_id=:me and is_delivered=0 and is_me=1 order by id"
	             );

	query.bindValue(":me", me_);
	query.exec();

	lasterr = query.lastError();

	if (lasterr.isValid())
		return M;

	while(query.next())
	{
		M[query.value(0).toByteArray()] << query.value(1).toString();
	}

	return M;
}

bool History::isOK()
{
	return  !lasterr.isValid();
}

QString History::GetLastError()
{
	return lasterr.text();
}
