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
        quint32 peer_id;
        bool is_me;
        bool is_delivered;
        quint32 timestamp;
        QString message;
    };

    History(QString storage, quint32 my_id);
    void Add(quint32 peer_id, QString message, bool is_me);//is_me - true - message from me to peer, false - from peer to me
    void SetDelivered(quint32 peer_id); //mark first undelivered message from me to peer as delivered
    QList<HistoryRecord> GetMessages(quint32 peer_id, quint32 start_timestamp, quint32 end_timestamp);
    QMap<quint32, QList<QString> > GetUndeliveredMessages();
    bool isOK();
    QString GetLastError();

private:
    QSqlDatabase dbconn;
    QSqlError lasterr;

    quint32 me_;
};

#endif // HISTORY_H
