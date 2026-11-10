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
#include "contacts.h"
#include "accounts.h"
#include "../PICA_client.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QSet>
#include <QDebug>

// NULL instead of an empty blob, so that "group_id is null" selects one-to-one messages
static QVariant blob_or_null(const QByteArray &b)
{
	return b.isEmpty() ? QVariant(QVariant::ByteArray) : QVariant(b);
}

static const char *record_columns = "contact_id, is_me, is_delivered, timestamp, message, msg_id, group_id, prev_hash, sig";

History::History(QString storage, QByteArray my_id, QString connectionName)
	: storage_(storage), connection_name_(connectionName), me_(my_id)
{
	dbconn = connectionName.isEmpty() ? QSqlDatabase::database() : QSqlDatabase::database(connectionName);
	dbconn.setDatabaseName(storage);

	if (!dbconn.open())
	{
		lasterr = dbconn.lastError();
		return;
	}

	QSqlQuery query(dbconn);

	query.exec("PRAGMA foreign_keys=ON;");

	//check result, if foreign keys support is on
}

bool History::Add(const HistoryRecord &r)
{
	QSqlQuery query(dbconn);

	query.prepare(
	    "insert or ignore into history (contact_id, account_id, is_me, is_delivered, message, id, timestamp, msg_id, group_id, prev_hash, sig) \
                values (:contact_id, :account_id, :is_me, :is_delivered, :message, null, :timestamp, :msg_id, :group_id, :prev_hash, :sig);"
	);

	query.bindValue(":contact_id", r.peer_id);
	query.bindValue(":account_id", me_);
	query.bindValue(":is_me", r.is_me ? 1 : 0);
	query.bindValue(":is_delivered", r.is_delivered ? 1 : 0);
	query.bindValue(":message", r.message);
	query.bindValue(":timestamp", r.timestamp);
	query.bindValue(":msg_id", r.msg_id);
	query.bindValue(":group_id", blob_or_null(r.group_id));
	query.bindValue(":prev_hash", blob_or_null(r.prev_hash));
	query.bindValue(":sig", blob_or_null(r.sig));

	query.exec();

	lasterr = query.lastError();

	return !lasterr.isValid() && query.numRowsAffected() == 1;
}

QList<History::HistoryRecord> History::select(QString where, const QMap<QString, QVariant> &binds)
{
	QList<HistoryRecord> L;
	HistoryRecord r;
	QSqlQuery query(dbconn);

	query.setForwardOnly(true);
	query.prepare(QString("select %1 from history where account_id=:me and %2").arg(record_columns).arg(where));
	query.bindValue(":me", me_);

	for (QMap<QString, QVariant>::const_iterator i = binds.constBegin(); i != binds.constEnd(); ++i)
		query.bindValue(i.key(), i.value());

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
		r.msg_id =       query.value(5).toByteArray();
		r.group_id =     query.value(6).toByteArray();
		r.prev_hash =    query.value(7).toByteArray();
		r.sig =          query.value(8).toByteArray();

		L << r;
	}

	return L;
}

static QString conv_where(bool is_group)
{
	return is_group ? "group_id=:conv" : "contact_id=:conv and group_id is null";
}

bool History::Get(QByteArray msg_id, HistoryRecord *r)
{
	QMap<QString, QVariant> b;
	b[":msg_id"] = msg_id;

	QList<HistoryRecord> L = select("msg_id=:msg_id", b);

	if (L.isEmpty())
		return false;

	*r = L.first();
	return true;
}

QList<History::HistoryRecord> History::GetMessages(QByteArray conv, bool is_group, QString keyword)
{
	QMap<QString, QVariant> b;
	b[":conv"] = conv;
	b[":keyword"] = QString("%%1%").arg(keyword);

	return select(conv_where(is_group) + " and message like :keyword order by id", b);
}

QList<History::HistoryRecord> History::GetMessages(QByteArray conv, bool is_group, quint32 start_timestamp, quint32 end_timestamp)
{
	QMap<QString, QVariant> b;
	b[":conv"] = conv;
	b[":start"] = start_timestamp;
	b[":end"] = end_timestamp;

	return select(conv_where(is_group) + " and timestamp >= :start and timestamp <= :end order by id", b);
}

QByteArray History::LastHash(QByteArray conv, bool is_group)
{
	QMap<QString, QVariant> b;
	b[":conv"] = conv;

	QList<HistoryRecord> L = select(conv_where(is_group) + " order by id desc limit 1", b);

	return L.isEmpty() ? QByteArray() : Hash(L.first());
}

QByteArray History::Author(const HistoryRecord &r) const
{
	return r.is_me ? me_ : r.peer_id;
}

QByteArray History::SignedData(const HistoryRecord &r) const
{
	QByteArray buf;
	QDataStream ds(&buf, QIODevice::WriteOnly);
	// one-to-one messages are bound to their recipient, so they can't be replayed to someone else
	QByteArray conv = !r.group_id.isEmpty() ? r.group_id : (r.is_me ? r.peer_id : me_);

	ds.setVersion(QDataStream::Qt_5_0);
	ds << (quint8)1 << r.msg_id << Author(r) << conv << r.timestamp << r.message << r.prev_hash;

	return buf;
}

QByteArray History::Hash(const HistoryRecord &r) const
{
	return QCryptographicHash::hash(SignedData(r), QCryptographicHash::Sha256);
}

QString History::CertOf(QByteArray id)
{
	if (id == me_)
	{
		QFile f(Accounts::GetCurrentAccount().cert_file);

		return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()) : QString();
	}

	return Contacts(storage_, me_, connection_name_).GetContactCert(id);
}

bool History::VerifySignature(const HistoryRecord &r)
{
	QByteArray author = Author(r);
	QByteArray cert = CertOf(author).toLatin1();
	QByteArray data = SignedData(r);

	if (cert.isEmpty() || r.sig.isEmpty() || author.size() != PICA_ID_SIZE)
		return false;

	return PICA_verify_by_cert(cert.constData(), (const unsigned char *)author.constData(),
	                           data.constData(), data.size(),
	                           (const unsigned char *)r.sig.constData(), r.sig.size()) == 1;
}

History::ChainReport History::VerifyChain(QByteArray conv, bool is_group)
{
	ChainReport rep = {0, 0, 0, 0, 0};
	QList<HistoryRecord> L = GetMessages(conv, is_group, 0, 0xFFFFFFFF);
	QSet<QByteArray> hashes;

	for (int i = 0; i < L.size(); i++)
		hashes.insert(Hash(L[i]));

	for (int i = 0; i < L.size(); i++)
	{
		rep.total++;

		if (L[i].sig.isEmpty())
			rep.unsigned_msgs++;
		else if (VerifySignature(L[i]))
			rep.valid++;
		else
			rep.bad_signatures++;

		if (!L[i].prev_hash.isEmpty() && !hashes.contains(L[i].prev_hash))
			rep.broken_links++;
	}

	return rep;
}

void History::OutboxAdd(QByteArray peer_id, QByteArray msg_id, int kind)
{
	QSqlQuery query(dbconn);

	query.prepare("insert or ignore into outbox (account_id, peer_id, msg_id, kind) values (:me, :peer_id, :msg_id, :kind)");
	query.bindValue(":me", me_);
	query.bindValue(":peer_id", peer_id);
	query.bindValue(":msg_id", msg_id);
	query.bindValue(":kind", kind);
	query.exec();

	lasterr = query.lastError();
}

QList<History::OutboxItem> History::Outbox(QByteArray peer_id)
{
	QList<OutboxItem> L;
	QSqlQuery query(dbconn);

	query.setForwardOnly(true);
	query.prepare("select msg_id, kind from outbox where account_id=:me and peer_id=:peer_id order by id");
	query.bindValue(":me", me_);
	query.bindValue(":peer_id", peer_id);
	query.exec();

	lasterr = query.lastError();

	while (query.next())
	{
		OutboxItem it;
		it.msg_id = query.value(0).toByteArray();
		it.kind = query.value(1).toInt();
		L << it;
	}

	return L;
}

QList<QByteArray> History::OutboxPeers()
{
	QList<QByteArray> L;
	QSqlQuery query(dbconn);

	query.setForwardOnly(true);
	query.prepare("select distinct peer_id from outbox where account_id=:me");
	query.bindValue(":me", me_);
	query.exec();

	lasterr = query.lastError();

	while (query.next())
		L << query.value(0).toByteArray();

	return L;
}

bool History::OutboxRemove(QByteArray peer_id, QByteArray msg_id)
{
	QSqlQuery query(dbconn);

	query.prepare("delete from outbox where account_id=:me and peer_id=:peer_id and msg_id=:msg_id");
	query.bindValue(":me", me_);
	query.bindValue(":peer_id", peer_id);
	query.bindValue(":msg_id", msg_id);
	query.exec();

	lasterr = query.lastError();

	if (lasterr.isValid() || query.numRowsAffected() == 0)
		return false;

	query.prepare("update history set is_delivered=1 where account_id=:me and msg_id=:msg_id and is_delivered=0 \
                  and not exists (select 1 from outbox where account_id=:me2 and msg_id=:msg_id2)");
	query.bindValue(":me", me_);
	query.bindValue(":msg_id", msg_id);
	query.bindValue(":me2", me_);
	query.bindValue(":msg_id2", msg_id);
	query.exec();

	lasterr = query.lastError();

	return !lasterr.isValid() && query.numRowsAffected() == 1;
}

quint32 History::LastSync(QByteArray peer_id, QByteArray conv)
{
	QSqlQuery query(dbconn);

	query.prepare("select last_sync from sync_state where account_id=:me and peer_id=:peer_id and conv_id=:conv");
	query.bindValue(":me", me_);
	query.bindValue(":peer_id", peer_id);
	query.bindValue(":conv", conv);
	query.exec();

	lasterr = query.lastError();

	return query.next() ? query.value(0).toUInt() : 0;
}

void History::SetLastSync(QByteArray peer_id, QByteArray conv, quint32 t)
{
	QSqlQuery query(dbconn);

	query.prepare("insert into sync_state (account_id, peer_id, conv_id, last_sync) values (:me, :peer_id, :conv, :t)");
	query.bindValue(":me", me_);
	query.bindValue(":peer_id", peer_id);
	query.bindValue(":conv", conv);
	query.bindValue(":t", t);
	query.exec();

	lasterr = query.lastError();
}

bool History::isOK()
{
	return  !lasterr.isValid();
}

QString History::GetLastError()
{
	return lasterr.text();
}

Groups::Groups(QString storage, QByteArray my_id, QString connectionName)
	: me_(my_id)
{
	dbconn = connectionName.isEmpty() ? QSqlDatabase::database() : QSqlDatabase::database(connectionName);
	dbconn.setDatabaseName(storage);
	dbconn.open();
}

bool Groups::Get(QByteArray id, GroupRecord *g)
{
	QSqlQuery query(dbconn);

	query.prepare("select owner_id, name, version, sig from groups where account_id=:me and id=:id");
	query.bindValue(":me", me_);
	query.bindValue(":id", id);
	query.exec();

	if (!query.next())
		return false;

	g->id = id;
	g->owner = query.value(0).toByteArray();
	g->name = query.value(1).toString();
	g->version = query.value(2).toUInt();
	g->sig = query.value(3).toByteArray();
	g->members.clear();

	query.prepare("select member_id from group_members where account_id=:me and group_id=:id");
	query.bindValue(":me", me_);
	query.bindValue(":id", id);
	query.exec();

	while (query.next())
		g->members << query.value(0).toByteArray();

	return true;
}

QList<Groups::GroupRecord> Groups::GetGroups()
{
	QList<GroupRecord> L;
	QList<QByteArray> ids;
	QSqlQuery query(dbconn);

	query.prepare("select group_id from group_members where account_id=:me and member_id=:me2");
	query.bindValue(":me", me_);
	query.bindValue(":me2", me_);
	query.exec();

	while (query.next())
		ids << query.value(0).toByteArray();

	for (int i = 0; i < ids.size(); i++)
	{
		GroupRecord g;

		if (Get(ids[i], &g))
			L << g;
	}

	return L;
}

void Groups::Store(const GroupRecord &g)
{
	QSqlQuery query(dbconn);

	dbconn.transaction();

	query.prepare("insert or replace into groups (account_id, id, name, owner_id, version, sig) values (:me, :id, :name, :owner, :version, :sig)");
	query.bindValue(":me", me_);
	query.bindValue(":id", g.id);
	query.bindValue(":name", g.name);
	query.bindValue(":owner", g.owner);
	query.bindValue(":version", g.version);
	query.bindValue(":sig", g.sig);
	query.exec();

	query.prepare("delete from group_members where account_id=:me and group_id=:id");
	query.bindValue(":me", me_);
	query.bindValue(":id", g.id);
	query.exec();

	for (int i = 0; i < g.members.size(); i++)
	{
		query.prepare("insert or ignore into group_members (account_id, group_id, member_id) values (:me, :id, :member)");
		query.bindValue(":me", me_);
		query.bindValue(":id", g.id);
		query.bindValue(":member", g.members[i]);
		query.exec();
	}

	dbconn.commit();
}

QByteArray Groups::SignedData(const GroupRecord &g)
{
	QByteArray buf;
	QDataStream ds(&buf, QIODevice::WriteOnly);
	QList<QByteArray> members = g.members;

	std::sort(members.begin(), members.end());
	ds.setVersion(QDataStream::Qt_5_0);
	ds << (quint8)2 << g.id << g.owner << g.version << g.name << members;

	return buf;
}
