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
