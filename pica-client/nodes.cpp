#include "nodes.h"

Nodes::Nodes(QString storage)
{
    dbconn=QSqlDatabase::addDatabase("QSQLITE");
    dbconn.setDatabaseName(storage);

    if (!dbconn.open())
    {
    lasterr=dbconn.lastError();
    return;
    }
//check if table nodes exists
}

void Nodes::Add(NodeRecord &n)
{
    QSqlQuery query;

    query.prepare("insert into nodes (address, port, last_active, inactive_count) values (:addr, :port, strftime('%s','now'), 0)");
    query.bindValue(":addr",n.address);
    query.bindValue(":port",n.port);
    query.exec();

    lasterr=query.lastError();
}

QList<Nodes::NodeRecord> Nodes::GetNodes()
{
    QList<NodeRecord> L;
    NodeRecord r;
    QSqlQuery query;

    query.setForwardOnly(true);
    query.exec("select address, port from nodes order by inactive_count asc, last_active desc");
    lasterr=query.lastError();

    if (lasterr.isValid())
        return L;

    while(query.next())
    {
        r.address =query.value(0).toString();
        r.port =query.value(1).toUInt();

        L<<r;
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

    query.bindValue(":addr",n.address);
    query.bindValue(":port",n.port);
    query.exec();

    lasterr=query.lastError();
}


