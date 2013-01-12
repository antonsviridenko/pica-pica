#include "history.h"

History::History(QString storage, quint32 my_id)
    : me_(my_id)
{
    dbconn=QSqlDatabase::addDatabase("QSQLITE");
    dbconn.setDatabaseName(storage);

    if (!dbconn.open())
    {
    lasterr=dbconn.lastError();
    return;
    }

    QSqlQuery query;

    query.exec("PRAGMA foreign_keys=ON;");

    //check result, if foreign keys support is on
}

void History::Add(quint32 peer_id, QString message, bool is_me)
{
    QSqlQuery query;

    query.prepare("insert into history (contact_id, account_id, timestamp, is_me, is_delivered, message) \
                  values (:peer_id, :me, strftime('%s','now'), :is_me, :is_delivered, :message)");

    query.bindValue(":peer_id", peer_id);
    query.bindValue(":me", me_);

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

    lasterr=query.lastError();
}

void History::SetDelivered(quint32 peer_id)
{
    QSqlQuery query;

    query.prepare("update history set is_delivered=1 where id in \
                  (select id from history where \
                   is_delivered=0  and is_me=1 and contact_id=:peer_id and account_id=:me order by id asc limit 1)"
                  );

    query.bindValue(":peer_id", peer_id);
    query.bindValue(":me", me_);

    query.exec();
    lasterr=query.lastError();
}

QList<History::HistoryRecord> History::GetMessages(quint32 peer_id, quint32 start_timestamp, quint32 end_timestamp)
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

    lasterr=query.lastError();

    if (lasterr.isValid())
        return L;

    while(query.next())
    {
        r.peer_id =      query.value(0).toUInt();
        r.is_me =        query.value(1).toBool();
        r.is_delivered = query.value(2).toBool();
        r.timestamp =    query.value(3).toUInt();
        r.message =      query.value(4).toString();

        L<<r;
    }

    return L;
}

QMap<quint32, QList<QString> >  History::GetUndeliveredMessages()
{
    QMap<quint32, QList<QString> > M;
    QSqlQuery query;

    query.setForwardOnly(true);
    query.prepare("select contact_id, message from history \
               where account_id=:me and is_delivered=0 and is_me=1 order by id"
            );

    query.bindValue(":me", me_);
    query.exec();

    lasterr=query.lastError();

    if (lasterr.isValid())
        return M;

    while(query.next())
    {
        M[query.value(0).toUInt()] << query.value(1).toString();
    }

    return M;
}
