#ifndef SKYNET_H
#define SKYNET_H

#include "nodes.h"
#include "nodethread.h"
#include "../PICA_client.h"
#include "../PICA_proto.h"
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

    void SendMessage(QByteArray to, QString msg);
    void SendFile(QByteArray to, QString filepath);
    void AcceptFile(QByteArray from, QString filepath);
    void DenyFile(QByteArray from);
    void PauseFile(QByteArray peer_id, bool pause_sending);
    void ResumeFile(QByteArray peer_id, bool resume_sending);
    void CancelFile(QByteArray peer_id, bool cancel_sending);

    Accounts::AccountRecord CurrentAccount() {return skynet_account;};

signals:
    void MessageReceived(QByteArray from, QString msg);
    void UnableToDeliver(QByteArray to, QString msg);
    void Delivered(QByteArray to);
    void BecameSelfAware();
    void LostSelfAwareness();
    void PeerCertificateReceived(QByteArray peer_id, QString cert_pem, bool *verified);
    void CertificateForged(QByteArray peer_id, QString received_cert, QString stored_cert);
    void ErrMsgFromNode(QString msg);
    void ContactsUpdated();
    void IncomingFileRequestReceived(QByteArray peer_id, quint64 file_size, QString filename);
    void OutgoingFileRequestAccepted(QByteArray peer_id);
    void OutgoingFileRequestDenied(QByteArray peer_id);
    void FileProgress(QByteArray peer_id, quint64 bytes_sent, quint64 bytes_received);
    void IncomingFilePaused(QByteArray peer_id);
    void IncomingFileCancelled(QByteArray peer_id);
    void IncomingFileIoError(QByteArray peer_id);
    void IncomingFileResumed(QByteArray peer_id);
    void OutgoingFilePaused(QByteArray peer_id);
    void OutgoingFileCancelled(QByteArray peer_id);
    void OutgoingFileIoError(QByteArray peer_id);
    void OutgoingFileResumed(QByteArray peer_id);
    void OutgoingFileFinished(QByteArray peer_id);
    void IncomingFileFinished(QByteArray peer_id);

private:
    Nodes nodes;
    QList<NodeThread*> threads;
    bool self_aware;
    QString status;
    struct PICA_conninfo *nodelink;
    Accounts::AccountRecord skynet_account;
    QMutex write_mutex;
    QMap<QByteArray, QList<QString> > msgqueues;
    QMap<QByteArray, QList<QString> > sndfilequeues;
    int timer_id;

    void timerEvent(QTimerEvent * e);

    void flush_queues(QByteArray to);
    struct PICA_chaninfo *find_active_chan(QByteArray peer_id);
    QList<QByteArray> filter_existing_chans(QList<QByteArray> peer_ids);

    void emit_MessageReceived(QByteArray from, QString msg);
    void emit_UnableToDeliver(QByteArray to, QString msg);
    void emit_Delivered(QByteArray to);
    void emit_PeerCertificateReceived(QByteArray peer_id, QString cert_pem, bool *verified);
    void emit_IncomingFileRequestReceived(QByteArray peer_id, quint64 file_size, QString filename);
    void emit_OutgoingFileRequestAccepted(QByteArray peer_id);
    void emit_OutgoingFileRequestDenied(QByteArray peer_id);
    void emit_FileProgress(QByteArray peer_id, quint64 bytes_sent, quint64 bytes_received);

    void emit_IncomingFilePaused(QByteArray peer_id);
    void emit_IncomingFileCancelled(QByteArray peer_id);
    void emit_IncomingFileIoError(QByteArray peer_id);
    void emit_IncomingFileResumed(QByteArray peer_id);
    void emit_OutgoingFilePaused(QByteArray peer_id);
    void emit_OutgoingFileCancelled(QByteArray peer_id);
    void emit_OutgoingFileIoError(QByteArray peer_id);
    void emit_OutgoingFileResumed(QByteArray peer_id);

    void emit_OutgoingFileFinished(QByteArray peer_id);
    void emit_IncomingFileFinished(QByteArray peer_id);

    //получение сообщения.
    static void newmsg_cb(const unsigned char *peer_id,const char* msgbuf,unsigned int nb,int type);
    //получение подтверждения о доставке сообщения
    static void msgok_cb(const unsigned char *peer_id);
    //создание канала с собеседником
    static void channel_established_cb(const unsigned char *peer_id);
    //создать канал не удалось
    static void channel_failed(const unsigned char *peer_id);
    //входящий запрос на создание канала от пользователя с номером caller_id
    static int accept_cb(const unsigned char *caller_id);
    //запрошенный пользователь не найден, в оффлайне или отказался от общения
    static void notfound_cb(const unsigned char *callee_id);

    static void channel_closed_cb(const unsigned char *peer_id, int reason);

    static void nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port);

    static int peer_cert_verify_cb(const unsigned char *peer_id, const char *cert_pem, unsigned int nb);

    static int accept_file_cb(const unsigned char  *peer_id, uint64_t file_size, const char *filename, unsigned int filename_size);

    static void accepted_file_cb(const unsigned char *peer_id);

    static void denied_file_cb(const unsigned char *peer_id);

    static void file_progress(const unsigned char *peer_id, uint64_t sent, uint64_t received);

    static void file_control(const unsigned char *peer_id, unsigned int sender_cmd, unsigned int receiver_cmd);

    static void file_finished(const unsigned char *peer_id, int sending);

private slots:
    void nodethread_finished();
    void nodethread_connected(QString addr, quint16 port, NodeThread *thread);
    void node_status_changed(QString addr,quint16 port,bool alive);
    void verify_peer_cert(QByteArray peer_id, QString cert_pem, bool *verified);

};

#endif // SKYNET_H
