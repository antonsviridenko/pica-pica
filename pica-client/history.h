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
#ifndef HISTORY_H
#define HISTORY_H

#include <QString>
#include <QtSql>
#include <QSqlDatabase>
#include <QList>
#include <QMap>

class History
{
public:
	struct HistoryRecord
	{
		QByteArray peer_id;
		bool is_me;
		bool is_delivered;
		quint32 timestamp;
		QString message;
	};

	History(QString storage, QByteArray my_id);
	void Add(QByteArray peer_id, QString message, bool is_me);//is_me - true - message from me to peer, false - from peer to me
	void SetDelivered(QByteArray peer_id); //mark first undelivered message from me to peer as delivered
	QList<HistoryRecord> GetMessages(QByteArray peer_id, quint32 start_timestamp, quint32 end_timestamp);
	QList<HistoryRecord> GetMessages(QByteArray peer_id, QString keyword);
	QMap<QByteArray, QList<QString> > GetUndeliveredMessages();
	bool isOK();
	QString GetLastError();

private:
	QSqlDatabase dbconn;
	QSqlError lasterr;

	QByteArray me_;
};

#endif // HISTORY_H
