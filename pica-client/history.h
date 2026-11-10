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

// A conversation ("conv") is identified by the peer's ID for one-to-one chats
// and by the group ID for group chats.
class History
{
public:
	struct HistoryRecord
	{
		QByteArray peer_id; // one-to-one: the peer; group: the author (my own ID for my messages)
		bool is_me;
		bool is_delivered;
		quint32 timestamp;
		QString message;
		QByteArray msg_id;
		QByteArray group_id; // empty for one-to-one messages
		QByteArray prev_hash; // Hash() of the latest message the author had in this conversation
		QByteArray sig; // author's signature of SignedData(), empty for messages from older clients
	};

	enum OutboxKind
	{
		OutboxMessage = 0, // msg_id refers to a history record
		OutboxGroupInfo = 1 // msg_id is a group ID, the current group info is sent
	};

	struct OutboxItem
	{
		QByteArray msg_id;
		int kind;
	};

	struct ChainReport
	{
		int total;
		int valid;
		int unsigned_msgs;
		int bad_signatures;
		int broken_links;
	};

	// See Nodes::Nodes() for what connectionName selects and why.
	History(QString storage, QByteArray my_id, QString connectionName = QString());

	bool Add(const HistoryRecord &r); // false if a record with this msg_id is already stored, or on error
	bool Get(QByteArray msg_id, HistoryRecord *r);
	QList<HistoryRecord> GetMessages(QByteArray conv, bool is_group, quint32 start_timestamp, quint32 end_timestamp);
	QList<HistoryRecord> GetMessages(QByteArray conv, bool is_group, QString keyword);
	QByteArray LastHash(QByteArray conv, bool is_group);

	QByteArray Author(const HistoryRecord &r) const;
	QByteArray SignedData(const HistoryRecord &r) const;
	QByteArray Hash(const HistoryRecord &r) const;
	bool VerifySignature(const HistoryRecord &r);
	ChainReport VerifyChain(QByteArray conv, bool is_group);
	QString CertOf(QByteArray id); // PEM certificate of my account or of a contact, empty if unknown

	void OutboxAdd(QByteArray peer_id, QByteArray msg_id, int kind);
	QList<OutboxItem> Outbox(QByteArray peer_id);
	QList<QByteArray> OutboxPeers();
	// Returns true when peer_id was the last recipient the message was waiting for
	bool OutboxRemove(QByteArray peer_id, QByteArray msg_id);

	quint32 LastSync(QByteArray peer_id, QByteArray conv);
	void SetLastSync(QByteArray peer_id, QByteArray conv, quint32 t);

	bool isOK();
	QString GetLastError();

private:
	QSqlDatabase dbconn;
	QSqlError lasterr;
	QString storage_;
	QString connection_name_;

	QByteArray me_;

	QList<HistoryRecord> select(QString where, const QMap<QString, QVariant> &binds);
};

class Groups
{
public:
	struct GroupRecord
	{
		QByteArray id;
		QByteArray owner;
		QString name;
		quint32 version;
		QList<QByteArray> members; // includes the owner
		QByteArray sig; // owner's signature of SignedData()
	};

	Groups(QString storage, QByteArray my_id, QString connectionName = QString());

	bool Get(QByteArray id, GroupRecord *g);
	QList<GroupRecord> GetGroups(); // groups I am a member of
	void Store(const GroupRecord &g);

	static QByteArray SignedData(const GroupRecord &g);

private:
	QSqlDatabase dbconn;
	QByteArray me_;
};

#endif // HISTORY_H
