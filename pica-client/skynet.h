#ifndef SKYNET_H
#define SKYNET_H

#include "nodes.h"
#include "nodethread.h"
#include "../PICA_client.h"
#include "accounts.h"
#include <QObject>
#include <QMap>


class SkyNet : public QObject
{
    Q_OBJECT
public:
    SkyNet();
    void Join(Accounts::AccountRecord &accrec);
    bool isSelfAware();
    QString& Status();
    void Exit();

    void SendMessage(quint32 to, QString msg);
    Accounts::AccountRecord CurrentAccount() {return skynet_account;};

signals:
    void MessageReceived(quint32 from, QString msg);
    void UnableToDeliver(quint32 to, QString msg);
    void Delivered(quint32 to);
    void BecameSelfAware();
    void LostSelfAwareness();
    void PeerCertificateReceived(quint32 peer_id, QString cert_pem, bool *verified);
    void CertificateForged(quint32 peer_id, QString received_cert, QString stored_cert);

private:
    Nodes nodes;
    QList<NodeThread*> threads;
    bool self_aware;
    QString status;
    struct PICA_conninfo *nodelink;
    Accounts::AccountRecord skynet_account;
    QMutex write_mutex;
    QMap<quint32, QList<QString> > msgqueues;
    int timer_id;

    void timerEvent(QTimerEvent * e);

    void flush_msgqueue(quint32 to);
    struct PICA_chaninfo *find_active_chan(quint32 peer_id);
    QList<quint32> filter_existing_chans(QList<quint32> peer_ids);

    void emit_MessageReceived(quint32 from, QString msg);
    void emit_UnableToDeliver(quint32 to, QString msg);
    void emit_Delivered(quint32 to);
    void emit_PeerCertificateReceived(quint32 peer_id, QString cert_pem, bool *verified);

    //получение сообщения.
    static void newmsg_cb(unsigned int peer_id,char* msgbuf,unsigned int nb,int type);
    //получение подтверждения о доставке сообщения
    static void msgok_cb(unsigned int peer_id);
    //создание канала с собеседником
    static void channel_established_cb(unsigned int peer_id);
    //создать канал не удалось
    static void channel_failed(unsigned int peer_id);
    //входящий запрос на создание канала от пользователя с номером caller_id
    static int accept_cb(unsigned int caller_id);
    //запрошенный пользователь не найден, в оффлайне или отказался от общения
    static void notfound_cb(unsigned int callee_id);

    static void channel_closed_cb(unsigned int peer_id, int reason);

    static void nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port);

    static int peer_cert_verify_cb(unsigned int peer_id, const char *cert_pem, unsigned int nb);

private slots:
    void nodethread_finished();
    void nodethread_connected(QString addr, quint16 port, NodeThread *thread);
    void node_status_changed(QString addr,quint16 port,bool alive);
    void verify_peer_cert(quint32 peer_id, QString cert_pem, bool *verified);

};

#endif // SKYNET_H
