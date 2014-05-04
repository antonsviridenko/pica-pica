#include "skynet.h"
#include "globals.h"
#include <QMutex>
//#include "dialogs/viewcertdialog.h"
#include "contacts.h"
#include "openssltool.h"
//#include <QMessageBox> //debug
//#include "dialogs/forgedcertdialog.h"
#include <QDebug>
#include "history.h"

SkyNet::SkyNet()
    : nodes(config_dbname), QObject(0)
{
    self_aware = false;

PICA_client_callbacks cbs = {
                            newmsg_cb,
                            msgok_cb,
                            channel_established_cb,
                            channel_failed,
                            accept_cb,
                            notfound_cb,
                            channel_closed_cb,
                            nodelist_cb,
                            peer_cert_verify_cb,
                            accept_file_cb,
                            accepted_file_cb,
                            denied_file_cb,
                            file_progress,
                            file_control
                            };

PICA_client_init(&cbs);

nodelink = NULL;

connect(this, SIGNAL(PeerCertificateReceived(QByteArray,QString,bool*)), this, SLOT(verify_peer_cert(QByteArray,QString,bool*)),Qt::DirectConnection);

}

void SkyNet::nodethread_finished()
{
//удалить здесь завершившийся поток
    for (int i=0;i<threads.count();i++)
    {
        if (threads[i]->isFinished())
        {
            delete threads.takeAt(i);
            break;
        }
    }

    if (threads.count() == 0)
    {
        self_aware = false;
        emit LostSelfAwareness();
    }
}

void SkyNet::nodethread_connected(QString addr, quint16 port, NodeThread *thread)
{
    if (self_aware)
    {
        emit BecameSelfAware();

    //restore old peer connections, if any
    QList<QByteArray> c2c_peer_ids;

    //load undelivered messages from history
    if (msgqueues.isEmpty())
    {
        History h(config_dbname, Accounts::GetCurrentAccount().id);

        msgqueues = h.GetUndeliveredMessages();
    }

    //load file transfers to be completed
    // sndfilequeues = ...

    QMap<QByteArray, QList<QString> > queues = msgqueues;

    queues.unite(sndfilequeues);

    c2c_peer_ids = queues.uniqueKeys();

    for (int i = 0; i < c2c_peer_ids.size(); i++)
    {
        int ret;
        write_mutex.lock();
        struct PICA_chaninfo *chan = NULL;

        ret = PICA_create_channel(nodelink, (const unsigned char*)c2c_peer_ids[i].constData(), &chan);

        qDebug()<<"restoring channel to "<<c2c_peer_ids[i]<<" ret ="<<ret<<"\n";
        write_mutex.unlock();
    }

    //start timer
    timer_id = startTimer(10000);
    }
}

void SkyNet::node_status_changed(QString addr, quint16 port, bool alive)
{
    Nodes::NodeRecord nr = {addr, port};
    nodes.UpdateStatus(nr, alive);
}

void SkyNet::verify_peer_cert(QByteArray peer_id, QString cert_pem, bool *verified)
{
//    ViewCertDialog vcd;
//    vcd.SetCert(cert_pem);
//    vcd.exec();
    Contacts cnt(config_dbname, Accounts::GetCurrentAccount().id);
    QString stored_cert;


    if (!cnt.Exists(peer_id) && cnt.isOK())
    {
        cnt.Add(peer_id, Contacts::temporary);
    }
    if ((stored_cert = cnt.GetContactCert(peer_id)).isEmpty())
    {
        cnt.SetContactCert(peer_id, cert_pem);

        QString name = OpenSSLTool::NameFromCertString(cert_pem);

        cnt.SetContactName(peer_id, name);

        emit ContactsUpdated();
    }
    else
    {//compare certificates
        QString strip_pem[4] = {
           "-----BEGIN CERTIFICATE-----",
           "-----END CERTIFICATE-----",
           "-----BEGIN X509 CERTIFICATE-----",
           "-----END X509 CERTIFICATE-----"
        };
        QString stripped_stored_cert = stored_cert;
        QString stripped_received_cert = cert_pem;

        for (int i = 0; i < 4; i++)
        {
            stripped_stored_cert.replace(strip_pem[i], "");
            stripped_received_cert.replace(strip_pem[i], "");
        }

        QByteArray stored_DER, received_DER;

        stored_DER = QByteArray::fromBase64(stripped_stored_cert.toAscii().constData());
        received_DER = QByteArray::fromBase64(stripped_received_cert.toAscii().constData());

        if (stored_DER.size() != received_DER.size() ||
        memcmp(stored_DER.constData(), received_DER.constData(), stored_DER.size()) != 0)
        {//put scary message here
            *verified = false;
            emit CertificateForged(peer_id, cert_pem, stored_cert);
        }


    }
}

void SkyNet::timerEvent(QTimerEvent *e)
{
    if (self_aware)
    {
        if (write_mutex.tryLock(10))
        {
            QMap<QByteArray, QList<QString> > queues = msgqueues;
            queues.unite(sndfilequeues);

            QList<QByteArray> c2c_peer_ids = queues.uniqueKeys();

            c2c_peer_ids = filter_existing_chans(c2c_peer_ids);

            for (int i = 0; i < c2c_peer_ids.size(); i++)
            {
                int ret;

                struct PICA_chaninfo *chan = NULL;

                ret = PICA_create_channel(nodelink, (const unsigned char*)c2c_peer_ids[i].constData(), &chan);

                qDebug()<<"restoring channel to "<<c2c_peer_ids[i]<<" ret ="<<ret<<" in timer event\n";

            }
            write_mutex.unlock();
        }
    }
else
    {
    killTimer(e->timerId());
    Accounts::AccountRecord acc = this->CurrentAccount();
    Join(acc);
    }
}

void SkyNet::Join(Accounts::AccountRecord &accrec)
{
    QList<Nodes::NodeRecord> nodelist;
    skynet_account = accrec;
    nodelist = nodes.GetNodes();

    if (nodelist.count()==0)
    {
        status = QObject::tr("No known Pica Pica nodes");
        return;
    }

    for (int i=0;i<nodelist.count() && !self_aware;i++)
    {//TODO FIXME сделать запуск потоков порциями, а не все сразу
        threads.append(new NodeThread(nodelist[i],&self_aware,skynet_account,&nodelink, &write_mutex));
        connect(threads.last(), SIGNAL(finished()),this,SLOT(nodethread_finished()));
        connect(threads.last(), SIGNAL(NodeStatusChanged(QString,quint16,bool)), this, SLOT(node_status_changed(QString,quint16,bool)));
        connect(threads.last(), SIGNAL(ConnectedToNode(QString,quint16,NodeThread*)), this, SLOT(nodethread_connected(QString,quint16,NodeThread*)));
        connect(threads.last(), SIGNAL(ErrorMsg(QString)), this, SIGNAL(ErrMsgFromNode(QString)));
    }

}

void SkyNet::Exit()
{
    for (int i = 0; i< threads.count(); i++)
        threads[i]->CloseThread();

    self_aware = false;

    killTimer(timer_id);
}

bool SkyNet::isSelfAware()
{
    return self_aware;
}

void SkyNet::SendFile(QByteArray to, QString filepath)
{
int ret = PICA_OK;
struct PICA_chaninfo *iptr;

write_mutex.lock();//<<

if ( (iptr = find_active_chan(to)) )
    {
        ret = PICA_send_file(iptr, filepath.toUtf8().data());

        if (ret != PICA_OK)
        {
            //show error somewhere somehow
        }

        write_mutex.unlock();//>>
        return;
    }

    if (sndfilequeues.contains(to))
    {
    //sending multiple files is not supported yet
    }
    else
    {
       struct PICA_chaninfo *chan = NULL;

       ret = PICA_create_channel(nodelink, (const unsigned char*)to.constData(), &chan);

       QList<QString> l;
       l.append(filepath);
       sndfilequeues[to] = l;
    }

write_mutex.unlock();//>>

    if (ret!= PICA_OK)
    {
    //report error
    }
}

void SkyNet::AcceptFile(QByteArray from, QString filepath)
{
int ret = PICA_OK;
struct PICA_chaninfo *iptr;

write_mutex.lock();//<<
if ( (iptr = find_active_chan(from)) )
    {
        ret = PICA_accept_file(iptr, filepath.toUtf8().data(), filepath.toUtf8().size());

        qDebug() << "PICA_accept_file(" << filepath.toUtf8().data() << ", " << filepath.toUtf8().size() << ") returned " << ret << "\n";
        if (ret != PICA_OK)
        {
            //show error somewhere somehow
        }
    }
write_mutex.unlock();//>>
}

void SkyNet::SendMessage(QByteArray to, QString msg)
{
int ret = PICA_OK;
struct PICA_chaninfo *iptr;

write_mutex.lock();//<<

if ( (iptr = find_active_chan(to)) )
{
    //write_mutex.lock();//<<
    ret = PICA_send_msg(iptr, msg.toUtf8().data(), msg.toUtf8().size());
    //write_mutex.unlock();//>>

    if (ret != PICA_OK)
        emit UnableToDeliver(to, msg);

    write_mutex.unlock();//>>
    return;
}

    if (msgqueues.contains(to))
    {
        msgqueues[to].append(msg);
    }
    else
    {
        struct PICA_chaninfo *chan = NULL;

        ret = PICA_create_channel(nodelink, (const unsigned char*)to.constData(), &chan);

        QList<QString> l;
        l.append(msg);
        msgqueues[to] = l;
    }


write_mutex.unlock();//>>

    if (ret != PICA_OK)
        emit UnableToDeliver(to, msg);

}

void SkyNet::flush_queues(QByteArray to)
{
    struct PICA_chaninfo *chan;
    int ret;

    if (self_aware && (msgqueues.contains(to) || sndfilequeues.contains(to)))
    {
        qDebug()<<"flushing "<<to<<" message queue\n";

        if ((chan = find_active_chan(to)))
        {
            /////// messages
            while( !msgqueues[to].empty() )
            {
                ret = PICA_send_msg(chan, msgqueues[to].first().toUtf8().data(), msgqueues[to].first().toUtf8().size());

                if (ret != PICA_OK)
                {
                    emit UnableToDeliver(to, msgqueues[to].first());
                    break;
                }

                msgqueues[to].removeFirst();

            }

            if (msgqueues[to].empty())
                msgqueues.remove(to);


           ///////// files
           while (!sndfilequeues[to].empty())
           {
                ret = PICA_send_file(chan, sndfilequeues[to].first().toUtf8().data());

                if (ret != PICA_OK)
                {
                    //report error somehow
                    break;
                }

                sndfilequeues[to].removeFirst();
           }

           if (sndfilequeues[to].empty())
                sndfilequeues.remove(to);
        }
    }
}

struct PICA_chaninfo * SkyNet::find_active_chan(QByteArray peer_id)
{
if (!nodelink)
  return NULL;

struct PICA_chaninfo *iptr = nodelink->chan_list_head;

while(iptr)
    {
    if (iptr->state == PICA_CHANSTATE_ACTIVE && QByteArray((const char*)iptr->peer_id, PICA_ID_SIZE) == peer_id)
        break;

        iptr = iptr->next;
    }

return iptr;
}

QList<QByteArray> SkyNet::filter_existing_chans(QList<QByteArray> peer_ids)
{
    QList<QByteArray> ret = peer_ids;

    if (!nodelink)
      return ret;

    struct PICA_chaninfo *iptr = nodelink->chan_list_head;

    while(iptr)
        {
        if (peer_ids.contains(QByteArray((const char*)iptr->peer_id, PICA_ID_SIZE)))
            ret.removeOne((const char*)iptr->peer_id);

        iptr = iptr->next;
        }

    return ret;
}

void SkyNet::emit_Delivered(QByteArray to)
{
emit Delivered(to);
}

void SkyNet::emit_MessageReceived(QByteArray from, QString msg)
{
emit MessageReceived(from, msg);
}

void SkyNet::emit_UnableToDeliver(QByteArray to, QString msg)
{
emit UnableToDeliver(to, msg);
}

void SkyNet::emit_PeerCertificateReceived(QByteArray peer_id, QString cert_pem, bool *verified)
{
    emit PeerCertificateReceived(peer_id, cert_pem, verified);
}

void SkyNet::emit_IncomingFileRequestReceived(QByteArray peer_id, quint64 file_size, QString filename)
{
    emit IncomingFileRequestReceived(peer_id, file_size, filename);
}

void SkyNet::emit_OutgoingFileRequestAccepted(QByteArray peer_id)
{
    emit OutgoingFileRequestAccepted(peer_id);
}

void SkyNet::emit_OutgoingFileRequestDenied(QByteArray peer_id)
{
    emit OutgoingFileRequestDenied(peer_id);
}

void SkyNet::emit_FileProgress(QByteArray peer_id, quint64 bytes_sent, quint64 bytes_received)
{
    emit FileProgress(peer_id, bytes_sent, bytes_received);
}

//callbacks

//all callbacks are executed in separate thread, created in Nodethread instance, REMEMBER THAT !!!
// write_mutex is locked

void SkyNet::newmsg_cb(const unsigned char *peer_id, const char *msgbuf, unsigned int nb, int type)
{
QString msg = QString::fromUtf8(msgbuf, nb);

skynet->emit_MessageReceived(QByteArray((const char*)peer_id, PICA_ID_SIZE), msg);
}

void SkyNet::msgok_cb(const unsigned char *peer_id)
{
skynet->emit_Delivered(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::channel_established_cb(const unsigned char *peer_id)
{
skynet->flush_queues(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::channel_failed(const unsigned char *peer_id)
{
    qDebug()<<"channel failed ("<<QByteArray((const char*)peer_id, PICA_ID_SIZE).toBase64()<<")\n";
}

int SkyNet::accept_cb(const unsigned char *caller_id)
{
return 1; //implement black list
}

void SkyNet::notfound_cb(const unsigned char *callee_id)
{
  qDebug()<<"not found ("<<QByteArray((const char*)callee_id, PICA_ID_SIZE).toBase64()<<")\n";

  /*
  if (skynet->msgqueues.contains(callee_id))
  {
    while ( ! skynet->msgqueues[callee_id].empty())
    {
      skynet->emit_UnableToDeliver(callee_id, skynet->msgqueues[callee_id].last());
      skynet->msgqueues[callee_id].removeLast();
    }
    // msgqueues.remove(callee_id) was forgotten
  }
  */
}

void SkyNet::channel_closed_cb(const unsigned char *peer_id, int reason)
{
    qDebug()<<"channel closed ("<<QByteArray((const char*)peer_id, PICA_ID_SIZE).toBase64()<<")\n";

}

void SkyNet::nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port)
{
    Nodes::NodeRecord nr = {addr_str, port};
    skynet->nodes.Add(nr);
}

int SkyNet::peer_cert_verify_cb(const unsigned char *peer_id, const char *cert_pem, unsigned int nb)
{
    bool verified = true;

    skynet->emit_PeerCertificateReceived(QByteArray((const char*)peer_id, PICA_ID_SIZE), QString::fromAscii(cert_pem, nb), &verified);

    if (!verified)
        return 0;

    return 1;
}

int SkyNet::accept_file_cb(const unsigned char *peer_id, uint64_t file_size, const char *filename, unsigned int filename_size)
{
    skynet->emit_IncomingFileRequestReceived(QByteArray((const char*)peer_id, PICA_ID_SIZE),
                 file_size, QString::fromUtf8(filename, filename_size));

    return 2;//??? accept later code
}

void SkyNet::accepted_file_cb(const unsigned char *peer_id)
{
    qDebug()<<"FILE: file was accepted by remote side\n";

    skynet->emit_OutgoingFileRequestAccepted(QByteArray((const char *)peer_id, PICA_ID_SIZE));
}

void SkyNet::denied_file_cb(const unsigned char *peer_id)
{
    skynet->emit_OutgoingFileRequestDenied(QByteArray((const char *)peer_id, PICA_ID_SIZE));
}

void SkyNet::file_progress(const unsigned char *peer_id, uint64_t sent, uint64_t received)
{
    qDebug()<<"FILE: file progress" << sent << " sent " << received << "received\n";
    skynet->emit_FileProgress(QByteArray((const char *)peer_id, PICA_ID_SIZE), sent, received);
}

void SkyNet::file_control(const unsigned char *peer_id, unsigned int sender_cmd, unsigned int receiver_cmd)
{
    //implement me
}

