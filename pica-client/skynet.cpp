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
                            peer_cert_verify_cb
                            };

PICA_client_init(&cbs);

nodelink = NULL;

connect(this, SIGNAL(PeerCertificateReceived(quint32,QString,bool*)), this, SLOT(verify_peer_cert(quint32,QString,bool*)),Qt::DirectConnection);

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
    QList<quint32> c2c_peer_ids;

    //load undelivered messages from history
    if (msgqueues.isEmpty())
    {
        History h(config_dbname, account_id);

        msgqueues = h.GetUndeliveredMessages();
    }

    c2c_peer_ids = msgqueues.keys();

    for (int i = 0; i < c2c_peer_ids.size(); i++)
    {
        int ret;
        write_mutex.lock();
        struct PICA_chaninfo *chan = NULL;

        ret = PICA_create_channel(nodelink, c2c_peer_ids[i], &chan);

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

void SkyNet::verify_peer_cert(quint32 peer_id, QString cert_pem, bool *verified)
{
//    ViewCertDialog vcd;
//    vcd.SetCert(cert_pem);
//    vcd.exec();
    Contacts cnt(config_dbname, account_id);
    QString stored_cert;

    if ((stored_cert = cnt.GetContactCert(peer_id)).isEmpty())
    {
        cnt.SetContactCert(peer_id, cert_pem);

        QString name = OpenSSLTool::NameFromCertString(cert_pem);

        cnt.SetContactName(peer_id, name);
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
    QList<quint32> c2c_peer_ids = msgqueues.keys();

    write_mutex.lock();

    c2c_peer_ids = filter_existing_chans(c2c_peer_ids);

    for (int i = 0; i < c2c_peer_ids.size(); i++)
    {
        int ret;

        struct PICA_chaninfo *chan = NULL;

        ret = PICA_create_channel(nodelink, c2c_peer_ids[i], &chan);

        qDebug()<<"restoring channel to "<<c2c_peer_ids[i]<<" ret ="<<ret<<" in timer event\n";

    }
    write_mutex.unlock();
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

void SkyNet::SendMessage(quint32 to, QString msg)
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

        ret = PICA_create_channel(nodelink, to, &chan);

        QList<QString> l;
        l.append(msg);
        msgqueues[to] = l;
    }


write_mutex.unlock();//>>

    if (ret != PICA_OK)
        emit UnableToDeliver(to, msg);

}

void SkyNet::flush_msgqueue(quint32 to)
{
    struct PICA_chaninfo *chan;
    int ret;

    if (self_aware && msgqueues.contains(to))
    {
        qDebug()<<"flushing "<<to<<" message queue\n";

        if ((chan = find_active_chan(to)))
        {
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
        }
    }
}

struct PICA_chaninfo * SkyNet::find_active_chan(quint32 peer_id)
{
if (!nodelink)
  return NULL;

struct PICA_chaninfo *iptr = nodelink->chan_list_head;

while(iptr)
    {
    if (iptr->state == PICA_CHANSTATE_ACTIVE && iptr->peer_id == peer_id)
        break;

        iptr = iptr->next;
    }

return iptr;
}

QList<quint32> SkyNet::filter_existing_chans(QList<quint32> peer_ids)
{
    QList<quint32> ret = peer_ids;

    if (!nodelink)
      return ret;

    struct PICA_chaninfo *iptr = nodelink->chan_list_head;

    while(iptr)
        {
        if (peer_ids.contains(iptr->peer_id))
            ret.removeOne(iptr->peer_id);

        iptr = iptr->next;
        }

    return ret;
}

void SkyNet::emit_Delivered(quint32 to)
{
emit Delivered(to);
}

void SkyNet::emit_MessageReceived(quint32 from, QString msg)
{
emit MessageReceived(from, msg);
}

void SkyNet::emit_UnableToDeliver(quint32 to, QString msg)
{
emit UnableToDeliver(to, msg);
}

void SkyNet::emit_PeerCertificateReceived(quint32 peer_id, QString cert_pem, bool *verified)
{
    emit PeerCertificateReceived(peer_id, cert_pem, verified);
}

//callbacks

//all callbacks are executed in separate thread, created in Nodethread instance, REMEMBER THAT !!!
// write_mutex is locked

void SkyNet::newmsg_cb(unsigned int peer_id, char *msgbuf, unsigned int nb, int type)
{
QString msg = QString::fromUtf8(msgbuf, nb);

skynet->emit_MessageReceived(peer_id, msg);
}

void SkyNet::msgok_cb(unsigned int peer_id)
{
skynet->emit_Delivered(peer_id);
}

void SkyNet::channel_established_cb(unsigned int peer_id)
{
skynet->flush_msgqueue(peer_id);
}

void SkyNet::channel_failed(unsigned int peer_id)
{
    qDebug()<<"channel failed ("<<peer_id<<")\n";
}

int SkyNet::accept_cb(unsigned int caller_id)
{
return 1; //implement black list
}

void SkyNet::notfound_cb(unsigned int callee_id)
{
  qDebug()<<"not found ("<<callee_id<<")\n";

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

void SkyNet::channel_closed_cb(unsigned int peer_id, int reason)
{
    qDebug()<<"channel closed ("<<peer_id<<")\n";

}

void SkyNet::nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port)
{
    Nodes::NodeRecord nr = {addr_str, port};
    skynet->nodes.Add(nr);
}

int SkyNet::peer_cert_verify_cb(unsigned int peer_id, const char *cert_pem, unsigned int nb)
{
    bool verified = true;

    skynet->emit_PeerCertificateReceived(peer_id, QString::fromAscii(cert_pem, nb), &verified);

    if (!verified)
        return 0;

    return 1;
}
