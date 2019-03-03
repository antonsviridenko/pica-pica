/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
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
#include "dhparam.h"
#include "askpassword.h"
#include "settings.h"
#include "../PICA_netconf.h"

SkyNet::SkyNet()
	: nodes(config_dbname), QObject(0)
{
	self_aware = false;

	PICA_client_callbacks cbs =
	{
		newmsg_cb,
		msgok_cb,
		c2c_established_cb,
		c2c_failed,
		accept_cb,
		notfound_cb,
		c2c_closed_cb,
		nodelist_cb,
		peer_cert_verify_cb,
		accept_file_cb,
		accepted_file_cb,
		denied_file_cb,
		file_progress,
		file_control,
		file_finished,
		c2n_established_cb,
		c2n_failed_cb,
		c2n_closed_cb,
		listener_error_cb,
		multilogin_cb
	};

	PICA_client_init(&cbs);

	this->active_nodelink = NULL;
	this->acc = NULL;
	this->listener = NULL;

	connect(this, SIGNAL(PeerCertificateReceived(QByteArray, QString, bool*)), this, SLOT(verify_peer_cert(QByteArray, QString, bool*)), Qt::DirectConnection);
	connect(this, SIGNAL(MultiloginMessageReceived(quint64,QString,quint16)), this, SLOT(multilogin_event(quint64,QString,quint16)), Qt::QueuedConnection);

	event_loop_timer_id = startTimer(100);

}

void SkyNet::nodelink_activated(PICA_c2n *c2n)
{
	for (int i = 0; i < connecting_nodes.size(); i++)
	{
		if (connecting_nodes[i].first == c2n)
		{
			node_status_changed(connecting_nodes[i].second, true);
			connecting_nodes.removeAt(i);
			break;
		}
	}

	if (self_aware)
	{
		connected_nodes_to_close.append(c2n);
	}
	else
	{
		self_aware = true;
		active_nodelink = c2n;

		emit BecameSelfAware();
		//
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
			struct PICA_c2c *chan = NULL;

			ret = PICA_new_c2c(active_nodelink, (const unsigned char*)c2c_peer_ids[i].constData(), NULL, &chan);

			qDebug() << "restoring c2c to " << c2c_peer_ids[i] << " ret =" << ret << "\n";
		}

		//start timer
		retry_timer_id = startTimer(10000);
		//
	}


}

void SkyNet::nodelink_closed(PICA_c2n *c2n, int error)
{
	if (self_aware && c2n == active_nodelink)
	{
		active_nodelink = NULL;
		emit LostSelfAwareness();
	}
	else
	{
		for (int i = 0; i < connecting_nodes.size(); i++)
		{
			if (connecting_nodes[i].first == c2n)
			{
				connecting_nodes.removeAt(i);
				break;
			}
		}
	}
}

void SkyNet::nodelink_failed(PICA_c2n *c2n, int error)
{
	for (int i = 0; i < connecting_nodes.size(); i++)
	{
		if (connecting_nodes[i].first == c2n)
		{
			switch(error)
			{
			case PICA_ERRPROTONEW:

				emit StatusMsg(QString("Node %1:%2 has newer protocol version. Please check if update for pica-client is available")
				               .arg(connecting_nodes[i].second.address)
				               .arg(connecting_nodes[i].second.port),
				               true);
				break;

			case PICA_ERRPROTOOLD:

				emit StatusMsg(QString("Node %1:%2 has older protocol. Disconnected.")
				               .arg(connecting_nodes[i].second.address)
				               .arg(connecting_nodes[i].second.port),
				               false);
				break;
			}

			node_status_changed(connecting_nodes[i].second, false);
			connecting_nodes.removeAt(i);
			break;
		}
	}
	if (connecting_nodes.empty())
		emit LostSelfAwareness();
}

void SkyNet::multilogin_event(quint64 timestamp, QString node_addr, quint16 node_port)
{
	emit StatusMsg(QString("%3 Detected new login of your account at node %1:%2")
				.arg(node_addr).arg(node_port).arg(QDateTime::fromTime_t(timestamp).toString("yyyy-MM-dd hh:mm:ss")),
				true);

	if (active_nodelink->multilogin_policy == PICA_MULTILOGIN_REPLACE)
	{
		emit StatusMsg(QString("Multilogin policy is set to \"replace\". Going offline..."), true);
		SkyNet::Exit();
	}
}

void SkyNet::node_status_changed(Nodes::NodeRecord nr, bool alive)
{
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
	{
		//compare certificates
		QString strip_pem[4] =
		{
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

		stored_DER = QByteArray::fromBase64(stripped_stored_cert.toLatin1().constData());
		received_DER = QByteArray::fromBase64(stripped_received_cert.toLatin1().constData());

		if (stored_DER.size() != received_DER.size() ||
		        memcmp(stored_DER.constData(), received_DER.constData(), stored_DER.size()) != 0)
		{
			//put scary message here
			*verified = false;
			emit CertificateForged(peer_id, cert_pem, stored_cert);
		}


	}
}

void SkyNet::timerEvent(QTimerEvent *e)
{
	if (e->timerId() == event_loop_timer_id)
	{
		if (connecting_nodes.size() > 0 || self_aware)
		{
			int ret;
			QVector<struct PICA_c2n *> nodelinks;

			nodelinks.reserve(connecting_nodes.size() + 1);

			for (int i = 0; i < connecting_nodes.size(); i++)
				nodelinks.append(connecting_nodes[i].first);

			if (self_aware)
				nodelinks.append(active_nodelink);

			nodelinks.append(NULL);

			ret = PICA_event_loop(nodelinks.data(), 25);

			while(!connected_nodes_to_close.empty())
			{
				PICA_close_c2n(connected_nodes_to_close.takeFirst());
			}

			if (ret != PICA_OK)
				emit StatusMsg("event loop error!", true);//show some error message
		}

		return;
	}


	if (self_aware)
	{
		QMap<QByteArray, QList<QString> > queues = msgqueues;
		queues.unite(sndfilequeues);

		QList<QByteArray> c2c_peer_ids = queues.uniqueKeys();

		c2c_peer_ids = filter_existing_chans(c2c_peer_ids);

		for (int i = 0; i < c2c_peer_ids.size(); i++)
		{
			int ret;

			struct PICA_c2c *chan = NULL;

			ret = PICA_new_c2c(active_nodelink, (const unsigned char*)c2c_peer_ids[i].constData(), NULL, &chan);

			qDebug() << "restoring c2c to " << c2c_peer_ids[i] << " ret =" << ret << " in timer event\n";

		}

	}
	else
	{
		killTimer(e->timerId());
		Accounts::AccountRecord acc = this->CurrentAccount();
		Join(acc);
	}
}

bool SkyNet::open_account()
{
	int ret;

	AskPassword::clear();
	do
	{
		ret = PICA_open_acc(skynet_account.cert_file.toUtf8().constData(),
		                    skynet_account.pkey_file.toUtf8().constData(),
		                    DHParam::GetDHParamFilename().toUtf8().constData(),
		                    AskPassword::ask_password_cb,
		                    &acc);

		if (ret == PICA_ERRINVPKEYPASSPHRASE)
			AskPassword::setInvalidPassword();
	}
	while (ret == PICA_ERRINVPKEYPASSPHRASE);

	qDebug() << "PICA_open_acc() returned " << ret;

	if (ret != PICA_OK)
		return false;

	return true;
}

void SkyNet::Join(Accounts::AccountRecord &accrec)
{
	PICA_directc2c_config directc2c_cfg = PICA_DIRECTC2C_CFG_DISABLED;
	int multilogin = PICA_MULTILOGIN_PROHIBIT;
	Settings st(config_dbname);

	skynet_account = accrec;

	QList<Nodes::NodeRecord> noderecords = nodes.GetNodes();

	if (!acc)
	{
		if (!open_account())
			return;
	}

	if (noderecords.count() == 0)
	{
		emit StatusMsg(QString(tr("No known Pica Pica nodes")), true);
		return;
	}

	directc2c_cfg = (PICA_directc2c_config)st.loadValue("direct_c2c.state", 1).toInt();
	multilogin = st.loadValue("multiple_logins.state", PICA_MULTILOGIN_PROHIBIT).toInt();

	if (!listener && directc2c_cfg == PICA_DIRECTC2C_CFG_ALLOWINCOMING)
	{
		int pub_port = st.loadValue("direct_c2c.public_port", 2298).toInt();
		int loc_port = st.loadValue("direct_c2c.local_port", 2298).toInt();
		QString public_addr = st.loadValue("direct_c2c.public_addr", "autoconfigure").toString();

		if (public_addr.contains("autoconfigure"))
		{
			in_addr_t guess;
			struct in_addr in;

			guess = PICA_guess_listening_addr_ipv4();
			in.s_addr = guess;
			public_addr = QString(inet_ntoa(in));
#ifdef HAVE_LIBMINIUPNPC
			if (st.loadValue("direct_c2c.upnp_enabled", 1).toBool() && PICA_is_reserved_addr_ipv4(guess))
			{
				int ret;
				char public_ip[64];
				ret = PICA_upnp_autoconfigure_ipv4(pub_port, loc_port, public_ip);

				if (ret)
				{
					public_addr = QString(public_ip);
				}
			}
#endif
			emit StatusMsg(QString(tr("Using autoconfigured address %1 port %2 for incoming direct connections")).arg(public_addr).arg(pub_port), false);
		}

		int ret = PICA_new_listener(acc,
		                            public_addr.toLatin1().constData(),
		                            pub_port,
		                            loc_port, &listener);
		if (ret != PICA_OK)
		{
			emit StatusMsg(QString(tr("Failed to open port %1 for incoming direct connections")).arg(loc_port), true);
			directc2c_cfg = PICA_DIRECTC2C_CFG_CONNECTONLY;
		}
	}

	if (!connecting_nodes.empty())
	{
		for (int i = 0; i < connecting_nodes.size(); i++)
			PICA_close_c2n(connecting_nodes[i].first);

		connecting_nodes.clear();
	}

	for (int i = 0; i < noderecords.size(); i++)
	{
		int ret;
		struct PICA_c2n *c2n = NULL;

		ret = PICA_new_c2n(acc, noderecords[i].address.toUtf8().constData(), noderecords[i].port,
		                   directc2c_cfg, multilogin, listener, &c2n);

		if (ret == PICA_OK)
		{
			connecting_nodes.append(QPair<struct PICA_c2n *, Nodes::NodeRecord>(c2n, noderecords[i]));
		}
		else
		{
			node_status_changed(noderecords[i], false);
		}
	}
	/*
	    for (int i=0;i<nodelist.count() && !self_aware;i++)
	    {//TODO FIXME сделать запуск потоков порциями, а не все сразу
	        threads.append(new NodeThread(nodelist[i],&self_aware, acc,&nodelink, &write_mutex));
	        connect(threads.last(), SIGNAL(finished()),this,SLOT(nodethread_finished()));
	        connect(threads.last(), SIGNAL(NodeStatusChanged(QString,quint16,bool)), this, SLOT(node_status_changed(QString,quint16,bool)));
	        connect(threads.last(), SIGNAL(ConnectedToNode(QString,quint16,NodeThread*)), this, SLOT(nodethread_connected(QString,quint16,NodeThread*)));
	        connect(threads.last(), SIGNAL(ErrorMsg(QString)), this, SIGNAL(ErrMsgFromNode(QString)));
		}*/

}

void SkyNet::Exit()
{
	if (self_aware && active_nodelink)
	{
		PICA_close_c2n(active_nodelink);
		active_nodelink = NULL;
	}

	self_aware = false;

	killTimer(retry_timer_id);

	msgqueues.clear();

	for (int i = 0; i < connecting_nodes.size(); i++)
		PICA_close_c2n(connecting_nodes[i].first);

	connecting_nodes.clear();

	emit LostSelfAwareness();
}

bool SkyNet::isSelfAware()
{
	return self_aware;
}

void SkyNet::SendFile(QByteArray to, QString filepath)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;



	if ( (iptr = find_active_chan(to)) )
	{
		ret = PICA_send_file(iptr, filepath.toUtf8().data());

		if (ret != PICA_OK)
		{
			//show error somewhere somehow
		}


		return;
	}

	if (sndfilequeues.contains(to))
	{
		//sending multiple files is not supported yet
	}
	else
	{
		struct PICA_c2c *chan = NULL;

		ret = PICA_new_c2c(active_nodelink, (const unsigned char*)to.constData(), NULL, &chan);

		QList<QString> l;
		l.append(filepath);
		sndfilequeues[to] = l;
	}



	if (ret != PICA_OK)
	{
		//report error
	}
}

void SkyNet::AcceptFile(QByteArray from, QString filepath)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;


	if ( (iptr = find_active_chan(from)) )
	{
		ret = PICA_accept_file(iptr, filepath.toUtf8().data(), filepath.toUtf8().size());

		qDebug() << "PICA_accept_file(" << filepath.toUtf8().data() << ", " << filepath.toUtf8().size() << ") returned " << ret << "\n";
		if (ret != PICA_OK)
		{
			//show error somewhere somehow
		}
	}

}

void SkyNet::DenyFile(QByteArray from)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;


	if ( (iptr = find_active_chan(from)) )
	{
		ret = PICA_deny_file(iptr);

		qDebug() << "PICA_deny_file() returned " << ret << "\n";
		if (ret != PICA_OK)
		{
			//show error somewhere somehow
		}
	}

}

void SkyNet::PauseFile(QByteArray peer_id, bool pause_sending)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;


	if ( (iptr = find_active_chan(peer_id)) )
	{
		qDebug() << "calling PICA_pause_file(" << iptr << "," << pause_sending << ")\n";
		ret = PICA_pause_file(iptr, (int)pause_sending );

		if (ret != PICA_OK)
		{
			qDebug() << "PICA_pause_file() returned " << ret << "\n";
		}
	}

}

void SkyNet::ResumeFile(QByteArray peer_id, bool resume_sending)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;


	if ( (iptr = find_active_chan(peer_id)) )
	{
		ret = PICA_resume_file(iptr, (int)resume_sending );

		if (ret != PICA_OK)
		{
			//show error somewhere somehow
		}
	}

}

void SkyNet::CancelFile(QByteArray peer_id, bool cancel_sending)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;


	if ( (iptr = find_active_chan(peer_id)) )
	{
		ret = PICA_cancel_file(iptr, (int)cancel_sending );

		if (ret != PICA_OK)
		{
			//show error somewhere somehow
		}
	}

}

void SkyNet::SendMessage(QByteArray to, QString msg)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;



	if ( (iptr = find_active_chan(to)) )
	{
		//write_mutex.lock();//<<
		ret = PICA_send_msg(iptr, msg.toUtf8().data(), msg.toUtf8().size());
		//write_mutex.unlock();//>>

		if (ret != PICA_OK)
			emit UnableToDeliver(to, msg);


		return;
	}

	if (msgqueues.contains(to))
	{
		msgqueues[to].append(msg);
	}
	else
	{
		struct PICA_c2c *chan = NULL;

		ret = PICA_new_c2c(active_nodelink, (const unsigned char*)to.constData(), NULL, &chan);

		QList<QString> l;
		l.append(msg);
		msgqueues[to] = l;
	}




	if (ret != PICA_OK)
		emit UnableToDeliver(to, msg);

}

void SkyNet::flush_queues(QByteArray to)
{
	struct PICA_c2c *chan;
	int ret;

	if (self_aware && (msgqueues.contains(to) || sndfilequeues.contains(to)))
	{
		qDebug() << "flushing " << to << " message queue\n";

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

struct PICA_c2c * SkyNet::find_active_chan(QByteArray peer_id)
{
	if (!active_nodelink)
		return NULL;

	struct PICA_c2c *iptr = active_nodelink->chan_list_head;

	while(iptr)
	{
		if (iptr->state == PICA_C2C_STATE_ACTIVE && QByteArray((const char*)iptr->peer_id, PICA_ID_SIZE) == peer_id)
			break;

		iptr = iptr->next;
	}

	return iptr;
}

QList<QByteArray> SkyNet::filter_existing_chans(QList<QByteArray> peer_ids)
{
	QList<QByteArray> ret = peer_ids;

	if (!active_nodelink)
		return ret;

	struct PICA_c2c *iptr = active_nodelink->chan_list_head;

	while(iptr)
	{
		if (peer_ids.contains(QByteArray((const char*)iptr->peer_id, PICA_ID_SIZE)))
			ret.removeOne(QByteArray((const char*)iptr->peer_id, PICA_ID_SIZE));

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

void SkyNet::emit_IncomingFilePaused(QByteArray peer_id)
{
	emit IncomingFilePaused(peer_id);
}

void SkyNet::emit_IncomingFileCancelled(QByteArray peer_id)
{
	emit IncomingFileCancelled(peer_id);
}

void SkyNet::emit_IncomingFileIoError(QByteArray peer_id)
{
	emit IncomingFileIoError(peer_id);
}

void SkyNet::emit_IncomingFileResumed(QByteArray peer_id)
{
	emit IncomingFileResumed(peer_id);
}

void SkyNet::emit_OutgoingFilePaused(QByteArray peer_id)
{
	emit OutgoingFilePaused(peer_id);
}

void SkyNet::emit_OutgoingFileCancelled(QByteArray peer_id)
{
	emit OutgoingFileCancelled(peer_id);
}

void SkyNet::emit_OutgoingFileIoError(QByteArray peer_id)
{
	emit OutgoingFileIoError(peer_id);
}

void SkyNet::emit_OutgoingFileResumed(QByteArray peer_id)
{
	emit OutgoingFileResumed(peer_id);
}

void SkyNet::emit_OutgoingFileFinished(QByteArray peer_id)
{
	emit OutgoingFileFinished(peer_id);
}

void SkyNet::emit_IncomingFileFinished(QByteArray peer_id)
{
	emit IncomingFileFinished(peer_id);
}

void SkyNet::emit_c2cClosed(QByteArray peer_id)
{
	emit c2cClosed(peer_id);
}

void SkyNet::emit_MultiloginMessageReceived(quint64 timestamp, QString node_addr, quint16 node_port)
{
	emit MultiloginMessageReceived(timestamp, node_addr, node_port);
}

//callbacks

void SkyNet::newmsg_cb(const unsigned char *peer_id, const char *msgbuf, unsigned int nb, int type)
{
	QString msg = QString::fromUtf8(msgbuf, nb);

	skynet->emit_MessageReceived(QByteArray((const char*)peer_id, PICA_ID_SIZE), msg);
}

void SkyNet::msgok_cb(const unsigned char *peer_id)
{
	skynet->emit_Delivered(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::c2c_established_cb(const unsigned char *peer_id)
{
	skynet->flush_queues(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::c2c_failed(const unsigned char *peer_id)
{
	qDebug() << "c2c failed (" << QByteArray((const char*)peer_id, PICA_ID_SIZE).toBase64() << ")\n";
}

int SkyNet::accept_cb(const unsigned char *caller_id)
{
	return 1; //implement black list
}

void SkyNet::notfound_cb(const unsigned char *callee_id)
{
	qDebug() << "not found (" << QByteArray((const char*)callee_id, PICA_ID_SIZE).toBase64() << ")\n";

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

void SkyNet::c2c_closed_cb(const unsigned char *peer_id, int reason)
{
	qDebug() << "c2c closed (" << QByteArray((const char*)peer_id, PICA_ID_SIZE).toBase64()
	         << ", error_code =" << reason << ")\n";

	skynet->emit_c2cClosed(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port)
{
	Nodes::NodeRecord nr = {addr_str, port};
	skynet->nodes.Add(nr);
}

int SkyNet::peer_cert_verify_cb(const unsigned char *peer_id, const char *cert_pem, unsigned int nb)
{
	bool verified = true;

	skynet->emit_PeerCertificateReceived(QByteArray((const char*)peer_id, PICA_ID_SIZE), QString::fromLatin1(cert_pem, nb), &verified);

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
	qDebug() << "FILE: file was accepted by remote side\n";

	skynet->emit_OutgoingFileRequestAccepted(QByteArray((const char *)peer_id, PICA_ID_SIZE));
}

void SkyNet::denied_file_cb(const unsigned char *peer_id)
{
	skynet->emit_OutgoingFileRequestDenied(QByteArray((const char *)peer_id, PICA_ID_SIZE));
}

void SkyNet::file_progress(const unsigned char *peer_id, uint64_t sent, uint64_t received)
{
	qDebug() << "FILE: file progress" << sent << " sent " << received << "received\n";
	skynet->emit_FileProgress(QByteArray((const char *)peer_id, PICA_ID_SIZE), sent, received);
}

void SkyNet::file_control(const unsigned char *peer_id, unsigned int sender_cmd, unsigned int receiver_cmd)
{
	qDebug() << "file_control callback: sender_cmd=" << sender_cmd << ", receiver_cmd=" << receiver_cmd << "\n";


	if (sender_cmd != PICA_PROTO_FILECONTROL_VOID && receiver_cmd == PICA_PROTO_FILECONTROL_VOID)
	{
		switch(sender_cmd)
		{
		case PICA_PROTO_FILECONTROL_PAUSE:
			skynet->emit_IncomingFilePaused(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_RESUME:
			skynet->emit_IncomingFileResumed(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_CANCEL:
			skynet->emit_IncomingFileCancelled(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_IOERROR:
			skynet->emit_IncomingFileIoError(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;
		}
	}
	else if (sender_cmd == PICA_PROTO_FILECONTROL_VOID && receiver_cmd != PICA_PROTO_FILECONTROL_VOID)
	{
		switch(receiver_cmd)
		{
		case PICA_PROTO_FILECONTROL_PAUSE:
			skynet->emit_OutgoingFilePaused(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_RESUME:
			skynet->emit_OutgoingFileResumed(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_CANCEL:
			skynet->emit_OutgoingFileCancelled(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_IOERROR:
			skynet->emit_OutgoingFileIoError(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;
		}
	}
}

void SkyNet::file_finished(const unsigned char *peer_id, int sending)
{
	qDebug() << "file_finished callback\n";

	if (sending)
		skynet->emit_OutgoingFileFinished(QByteArray((const char *)peer_id, PICA_ID_SIZE));
	else
		skynet->emit_IncomingFileFinished(QByteArray((const char *)peer_id, PICA_ID_SIZE));
}

void SkyNet::c2n_established_cb(struct PICA_c2n *c2n)
{
	skynet->nodelink_activated(c2n);
}

void SkyNet::c2n_failed_cb(struct PICA_c2n *c2n, int error)
{
	skynet->nodelink_failed(c2n, error);
}

void SkyNet::c2n_closed_cb(struct PICA_c2n *c2n, int error)
{
	skynet->nodelink_closed(c2n, error);
}

void SkyNet::listener_error_cb(struct PICA_listener *lst, int errorcode)
{

}

void SkyNet::multilogin_cb(uint64_t timestamp, void *addr_bin, const char *addr_str, uint16_t port)
{
	skynet->emit_MultiloginMessageReceived(timestamp, QString(addr_str), port);
}
