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
#include "skynet.h"
#include "globals.h"
#include <QMutex>
#include <QMetaType>
#include <QCoreApplication>
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
#include <time.h>

SkyNet::SkyNet()
	: nodes(nullptr), QObject(0), active_filetransfers(0)
{
	self_aware = false;
	call_waiting_c2c_connect = false;

	qRegisterMetaType<Accounts::AccountRecord>("Accounts::AccountRecord");

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
		multilogin_cb,
		direct_c2c_established_cb,
		incoming_call_cb,
		call_picked_up_cb,
		call_rejected_cb,
		call_hangup_cb,
		call_audio_params_cb,
		call_video_params_cb,
		call_audio_packet_cb,
		call_video_packet_cb,
		call_media_transport_cb

	};

	PICA_client_init(&cbs);

	this->active_nodelink = NULL;
	this->acc = NULL;
	this->listener = NULL;

	connect(this, SIGNAL(PeerCertificateReceived(QByteArray, QString, bool*)), this, SLOT(verify_peer_cert(QByteArray, QString, bool*)), Qt::DirectConnection);
	connect(this, SIGNAL(MultiloginMessageReceived(quint64,QString,quint16)), this, SLOT(multilogin_event(quint64,QString,quint16)), Qt::QueuedConnection);

	file_transfer_timer_id = 0;
	call_timer_id = 0;
	call_active = false;

}

SkyNet::~SkyNet()
{
	delete nodes;
}

// Name of the QSqlDatabase connection opened for this SkyNet instance's own
// (network) thread - separate from the GUI thread's default connection,
// since a QSqlDatabase connection can only be used by the thread that
// created it.
static const QString network_sql_connection_name = QStringLiteral("network_thread_connection");

void SkyNet::init()
{
	QSqlDatabase netdb = QSqlDatabase::addDatabase("QSQLITE", network_sql_connection_name);
	netdb.setDatabaseName(config_dbname);
	netdb.open();

	nodes = new Nodes(config_dbname, network_sql_connection_name);

	event_loop_timer_id = startTimer(100);
	c2c_reconnect_timer_id = startTimer(1000);
}

void SkyNet::moveBackToMainThread()
{
	moveToThread(QCoreApplication::instance()->thread());
}

void SkyNet::active_filetransfers_up()
{
	active_filetransfers++;

	if (active_filetransfers > 0 && file_transfer_timer_id == 0)
	{
		file_transfer_timer_id = startTimer(0);
	}
}

void SkyNet::active_filetransfers_down()
{
	active_filetransfers--;

	Q_ASSERT(active_filetransfers >= 0);

	if (active_filetransfers == 0 && file_transfer_timer_id != 0)
	{
		killTimer(file_transfer_timer_id);
		file_transfer_timer_id = 0;
	}
}

void SkyNet::call_active_set(bool active, QByteArray peer)
{
	if (active)
	{
		call_active_peer = peer;
	}
	else
	{
		// A c2c connection to some other contact going down says nothing
		// about the call in progress.
		if (!peer.isEmpty() && !call_active_peer.isEmpty() && peer != call_active_peer)
			return;

		call_active_peer.clear();
	}

	if (active == call_active)
		return;

	call_active = active;

	// Same trick as active_filetransfers_up(): a zero interval timer is not
	// a busy loop here, because PICA_event_loop() spends its time blocked
	// inside select(). Without it the network thread only enters select()
	// once per event_loop_timer_id tick, and an incoming media packet waits
	// for the rest of that interval before it is read.
	if (active && call_timer_id == 0)
	{
		call_timer_id = startTimer(0);
	}
	else if (!active && call_timer_id != 0)
	{
		killTimer(call_timer_id);
		call_timer_id = 0;
	}
}

void SkyNet::active_filetransfers_reset()
{
	active_filetransfers = 0;

	if (file_transfer_timer_id != 0)
	{
		killTimer(file_transfer_timer_id);
		file_transfer_timer_id = 0;
	}
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

		nodes->MakeClean();

		emit BecameSelfAware();

		//load undelivered messages from history
		if (msgqueues.isEmpty())
		{
			History h(config_dbname, Accounts::GetCurrentAccount().id, network_sql_connection_name);

			msgqueues = h.GetUndeliveredMessages();
		}

		//load file transfers to be completed
		// sndfilequeues = ...
		c2c_reconnect_timeouts.clear();
		reconnect_c2c();
	}
}

void SkyNet::nodelink_closed(PICA_c2n *c2n, int error)
{
	if (self_aware && c2n == active_nodelink)
	{
		active_nodelink = NULL;
		self_aware = false;
		emit LostSelfAwareness();
		nodelink_reconnect_timer_id = startTimer(10000);
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
	{
		self_aware = false;
		emit LostSelfAwareness();
		nodelink_reconnect_timer_id = startTimer(10000);
	}
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
	nodes->UpdateStatus(nr, alive);
}

void SkyNet::verify_peer_cert(QByteArray peer_id, QString cert_pem, bool *verified)
{
//    ViewCertDialog vcd;
//    vcd.SetCert(cert_pem);
//    vcd.exec();
	Contacts cnt(config_dbname, Accounts::GetCurrentAccount().id, network_sql_connection_name);
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

void SkyNet::update_c2c_reconnect_timeout(QByteArray peer_id)
{
	if (c2c_reconnect_timeouts.contains(QByteArray((const char*)peer_id, PICA_ID_SIZE)))
		c2c_reconnect_timeouts[QByteArray((const char*)peer_id, PICA_ID_SIZE)] *= 2;
	else
		c2c_reconnect_timeouts[QByteArray((const char*)peer_id, PICA_ID_SIZE)] = 1;

	if (c2c_reconnect_timeouts[QByteArray((const char*)peer_id, PICA_ID_SIZE)] >= 64)
		c2c_reconnect_timeouts[QByteArray((const char*)peer_id, PICA_ID_SIZE)] = 64;
}

void SkyNet::reset_c2c_reconnect_timeout(QByteArray peer_id)
{
	c2c_reconnect_timeouts.remove(peer_id);
}

void SkyNet::reconnect_c2c()
{
	if (msgqueues.empty() && sndfilequeues.empty())
		return;

	if (self_aware)
	{
		QMap<QByteArray, QList<QString> > queues = msgqueues;
		queues.unite(sndfilequeues);
		time_t t = time(NULL);

		QList<QByteArray> c2c_peer_ids = queues.uniqueKeys();

		c2c_peer_ids = filter_existing_chans(c2c_peer_ids);

		for (int i = 0; i < c2c_peer_ids.size(); i++)
		{
			int ret;

			struct PICA_c2c *chan = NULL;

			if (c2c_reconnect_timeouts.contains(c2c_peer_ids[i]))
			{
				if (t % c2c_reconnect_timeouts[c2c_peer_ids[i]] != 0)
					continue;
			}

			ret = PICA_new_c2c(active_nodelink, (const unsigned char*)c2c_peer_ids[i].constData(), NULL, &chan);

			qDebug() << "restoring c2c to " << c2c_peer_ids[i].toBase64() << " ret =" << ret << " in timer event\n";

		}
	}
}

void SkyNet::timerEvent(QTimerEvent *e)
{
	if (e->timerId() == file_transfer_timer_id || e->timerId() == event_loop_timer_id
	        || e->timerId() == call_timer_id)
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

			// A shorter select() timeout while a call is in progress: the
			// audio and video threads hand their packets over with a queued
			// invokeMethod(), and Qt cannot deliver those to this thread
			// while it sits inside select(). The timeout is therefore the
			// worst case delay added to every outgoing media packet.
			ret = PICA_event_loop(nodelinks.data(), call_active ? 5 : 25);

			while(!connected_nodes_to_close.empty())
			{
				PICA_close_c2n(connected_nodes_to_close.takeFirst());
			}

			if (ret != PICA_OK)
				emit StatusMsg("event loop error!", true);//show some error message
		}

		return;
	}

	if (self_aware && e->timerId() == c2c_reconnect_timer_id)
		reconnect_c2c();

	if (!self_aware && e->timerId() == nodelink_reconnect_timer_id)
	{
		killTimer(nodelink_reconnect_timer_id);
		Join(skynet_account);
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

void SkyNet::Join(const Accounts::AccountRecord &accrec)
{
	QMetaObject::invokeMethod(this, "do_Join", Qt::AutoConnection,
	                           Q_ARG(Accounts::AccountRecord, accrec));
}

void SkyNet::do_Join(const Accounts::AccountRecord &accrec)
{
	PICA_directc2c_config directc2c_cfg = PICA_DIRECTC2C_CFG_DISABLED;
	int multilogin = PICA_MULTILOGIN_PROHIBIT;
	Settings st(config_dbname, network_sql_connection_name);

	skynet_account = accrec;

	QList<Nodes::NodeRecord> noderecords = nodes->GetNodes();

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
}

void SkyNet::Exit()
{
	QMetaObject::invokeMethod(this, "do_Exit", Qt::AutoConnection);
}

void SkyNet::do_Exit()
{
	if (self_aware && active_nodelink)
	{
		PICA_close_c2n(active_nodelink);
		active_nodelink = NULL;
	}

	self_aware = false;

	active_filetransfers_reset();
	call_active_set(false, QByteArray());

	msgqueues.clear();

	for (int i = 0; i < connecting_nodes.size(); i++)
		PICA_close_c2n(connecting_nodes[i].first);

	connecting_nodes.clear();

	emit LostSelfAwareness();
}

void SkyNet::SendFile(QByteArray to, QString filepath)
{
	QMetaObject::invokeMethod(this, "do_SendFile", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to), Q_ARG(QString, filepath));
}

void SkyNet::do_SendFile(QByteArray to, QString filepath)
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
	QMetaObject::invokeMethod(this, "do_AcceptFile", Qt::AutoConnection,
	                           Q_ARG(QByteArray, from), Q_ARG(QString, filepath));
}

void SkyNet::do_AcceptFile(QByteArray from, QString filepath)
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
		else
			active_filetransfers_up();
	}

}

void SkyNet::DenyFile(QByteArray from)
{
	QMetaObject::invokeMethod(this, "do_DenyFile", Qt::AutoConnection,
	                           Q_ARG(QByteArray, from));
}

void SkyNet::do_DenyFile(QByteArray from)
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
	QMetaObject::invokeMethod(this, "do_PauseFile", Qt::AutoConnection,
	                           Q_ARG(QByteArray, peer_id), Q_ARG(bool, pause_sending));
}

void SkyNet::do_PauseFile(QByteArray peer_id, bool pause_sending)
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
	QMetaObject::invokeMethod(this, "do_ResumeFile", Qt::AutoConnection,
	                           Q_ARG(QByteArray, peer_id), Q_ARG(bool, resume_sending));
}

void SkyNet::do_ResumeFile(QByteArray peer_id, bool resume_sending)
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
	QMetaObject::invokeMethod(this, "do_CancelFile", Qt::AutoConnection,
	                           Q_ARG(QByteArray, peer_id), Q_ARG(bool, cancel_sending));
}

void SkyNet::do_CancelFile(QByteArray peer_id, bool cancel_sending)
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
	QMetaObject::invokeMethod(this, "do_SendMessage", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to), Q_ARG(QString, msg));
}

void SkyNet::do_SendMessage(QByteArray to, QString msg)
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

void SkyNet::AcceptCall(QByteArray from)
{
	QMetaObject::invokeMethod(this, "do_AcceptCall", Qt::AutoConnection,
	                           Q_ARG(QByteArray, from));
}

void SkyNet::do_AcceptCall(QByteArray from)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(from)))
	{
		ret = PICA_pickup_call(iptr);

		if (ret != PICA_OK)
			emit CallFailed(from, QString(tr("Failed to pickup the call: %1")).arg(ret));
		else
			call_active_set(true, from);

		return;
	}

}

void SkyNet::RejectCall(QByteArray from)
{
	QMetaObject::invokeMethod(this, "do_RejectCall", Qt::AutoConnection,
	                           Q_ARG(QByteArray, from));
}

void SkyNet::do_RejectCall(QByteArray from)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(from)))
	{
		ret = PICA_reject_call(iptr);

		if (ret != PICA_OK)
			emit CallFailed(from, QString(tr("Failed to reject the call: %1")).arg(ret));

		call_active_set(false, from);

		return;
	}

}

void SkyNet::HangupCall(QByteArray with)
{
	QMetaObject::invokeMethod(this, "do_HangupCall", Qt::AutoConnection,
	                           Q_ARG(QByteArray, with));
}

void SkyNet::do_HangupCall(QByteArray with)
{
	int ret = PICA_OK;
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(with)))
	{
		ret = PICA_hangup_call(iptr);

		if (ret != PICA_OK)
			emit CallFailed(with, QString(tr("Failed to hang up the call: %1")).arg(ret));

		call_active_set(false, with);

		return;
	}

}

void SkyNet::SendAudioParams(QByteArray to, QString codec, quint16 sample_rate)
{
	QMetaObject::invokeMethod(this, "do_SendAudioParams", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to), Q_ARG(QString, codec), Q_ARG(quint16, sample_rate));
}

void SkyNet::do_SendAudioParams(QByteArray to, QString codec, quint16 sample_rate)
{
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(to)))
	{
		PICA_set_call_audio_params(iptr, codec.toLatin1().constData(), sample_rate);
	}
}

void SkyNet::SendAudioPacket(QByteArray to, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	QMetaObject::invokeMethod(this, "do_SendAudioPacket", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to), Q_ARG(quint16, seq_num), Q_ARG(quint32, timestamp), Q_ARG(QByteArray, data));
}

void SkyNet::do_SendAudioPacket(QByteArray to, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(to)))
	{
		PICA_send_audio_packet(iptr, seq_num, timestamp, data.constData(), data.size());
	}
}

void SkyNet::SendVideoParams(QByteArray to, QString codec, quint16 width, quint16 height)
{
	QMetaObject::invokeMethod(this, "do_SendVideoParams", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to), Q_ARG(QString, codec), Q_ARG(quint16, width), Q_ARG(quint16, height));
}

void SkyNet::do_SendVideoParams(QByteArray to, QString codec, quint16 width, quint16 height)
{
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(to)))
	{
		PICA_set_call_video_params(iptr, codec.toLatin1().constData(), width, height);
	}
}

void SkyNet::SendVideoPacket(QByteArray to, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	QMetaObject::invokeMethod(this, "do_SendVideoPacket", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to), Q_ARG(quint16, seq_num), Q_ARG(quint32, timestamp), Q_ARG(QByteArray, data));
}

void SkyNet::do_SendVideoPacket(QByteArray to, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(to)))
	{
		PICA_send_video_packet(iptr, seq_num, timestamp, data.constData(), data.size());
	}
}

void SkyNet::StartCall(QByteArray to)
{
	QMetaObject::invokeMethod(this, "do_StartCall", Qt::AutoConnection,
	                           Q_ARG(QByteArray, to));
}

void SkyNet::do_StartCall(QByteArray to)
{
	if (!self_aware)
	{
		emit CallFailed(to, QString(tr("Pica Pica client is not online")));
		return;
	}

	int ret = PICA_OK;
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(to)))
	{
		ret = PICA_start_call(iptr);

		if (ret != PICA_OK)
			emit CallFailed(to, QString(tr("Failed to initiate the call: %1")).arg(ret));

		return;
	}

	call_waiting_c2c_connect = true;
	call_waiting_to = to;

	struct PICA_c2c *chan = NULL;

	ret = PICA_new_c2c(active_nodelink, (const unsigned char*)to.constData(), NULL, &chan);

	if (ret != PICA_OK)
		emit CallFailed(to, QString(tr("Failed to create the C2C connection for the call: %1")).arg(ret));
}

bool SkyNet::is_call_waiting_for(const QByteArray &peer_id)
{
	if (call_waiting_c2c_connect && call_waiting_to == peer_id)
		return true;

	return false;
}

void SkyNet::continue_start_call()
{
	int ret = PICA_ERRCALLNOTINPROGRESS;
	struct PICA_c2c *iptr;

	if ((iptr = find_active_chan(call_waiting_to)))
	{
		ret = PICA_start_call(iptr);
	}

	if (ret != PICA_OK)
	{
		emit CallFailed(call_waiting_to, QString(tr("Failed to initiate the call: %1")).arg(ret));
		call_waiting_c2c_connect = false;
	}
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

void SkyNet::emit_ConnectionStatusUpdated(QByteArray peer_id, QString status)
{
	emit ConnectionStatusUpdated(peer_id, status);
}

void SkyNet::emit_CallFailed(QByteArray peer_id, QString reason)
{
	emit CallFailed(peer_id, reason);
}

void SkyNet::emit_IncomingCall(QByteArray from)
{
	emit IncomingCall(from);
}

void SkyNet::emit_CallAccepted(QByteArray by)
{
	emit CallAccepted(by);
}

void SkyNet::emit_CallRejected(QByteArray by)
{
	emit CallRejected(by);
}

void SkyNet::emit_CallHungup(QByteArray by)
{
	emit CallHungup(by);
}

void SkyNet::emit_IncomingAudioParams(QByteArray peer_id, QString codec, quint16 sample_rate)
{
	emit IncomingAudioParams(peer_id, codec, sample_rate);
}

void SkyNet::emit_IncomingAudioPacket(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	emit IncomingAudioPacket(peer_id, seq_num, timestamp, data);
}

void SkyNet::emit_IncomingVideoParams(QByteArray peer_id, QString codec, quint16 width, quint16 height)
{
	emit IncomingVideoParams(peer_id, codec, width, height);
}

void SkyNet::emit_IncomingVideoPacket(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	emit IncomingVideoPacket(peer_id, seq_num, timestamp, data);
}

void SkyNet::emit_CallMediaTransportChanged(QByteArray peer_id, bool direct_udp, QString ciphersuitename, quint32 max_payload)
{
	emit CallMediaTransportChanged(peer_id, direct_udp, ciphersuitename, max_payload);
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

void SkyNet::c2c_established_cb(const unsigned char *peer_id, const char *ciphersuitename)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);
	skynet->reset_c2c_reconnect_timeout(peer);
	skynet->emit_ConnectionStatusUpdated(peer, QString("🔐: %1 c2c").arg(ciphersuitename));
	skynet->flush_queues(peer);

	if (skynet->is_call_waiting_for(peer))
	{
		skynet->continue_start_call();
	}
}

void SkyNet::c2c_failed(const unsigned char *peer_id)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);

	skynet->update_c2c_reconnect_timeout(peer);
	qDebug() << "c2c failed (" << peer.toBase64() << ")\n";
	skynet->emit_ConnectionStatusUpdated(peer, QString(tr("Failed to connect")));

	if (skynet->is_call_waiting_for(peer))
	{
		skynet->emit_CallFailed(peer, QString(tr("Failed to establish the C2C connection to %1")).arg(QString(peer.toBase64())));
		skynet->call_waiting_c2c_connect = false;
	}
}

int SkyNet::accept_cb(const unsigned char *caller_id)
{
	return 1; //implement black list
}

void SkyNet::notfound_cb(const unsigned char *callee_id)
{
	QByteArray peer((const char*)callee_id, PICA_ID_SIZE);

	skynet->update_c2c_reconnect_timeout(peer);
	qDebug() << "not found (" << peer.toBase64() << ")\n";

	if (skynet->is_call_waiting_for(peer))
	{
		skynet->emit_CallFailed(peer, QString(tr("Requested peer was not found online in the Pica Pica network")));
		skynet->call_waiting_c2c_connect = false;
	}
}

void SkyNet::c2c_closed_cb(const unsigned char *peer_id, int reason)
{
	qDebug() << "c2c closed (" << QByteArray((const char*)peer_id, PICA_ID_SIZE).toBase64()
	         << ", error_code =" << reason << ")\n";

	skynet->emit_c2cClosed(QByteArray((const char*)peer_id, PICA_ID_SIZE));
	skynet->emit_ConnectionStatusUpdated(QByteArray((const char*)peer_id, PICA_ID_SIZE), QString(tr("Disconnected")));
	/* a call carried by this connection cannot continue */
	skynet->call_active_set(false, QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port)
{
	Nodes::NodeRecord nr = {addr_str, static_cast<quint16>(port)};
	skynet->nodes->Add(nr);
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

	skynet->active_filetransfers_up();
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
			skynet->active_filetransfers_down();
			skynet->emit_IncomingFilePaused(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_RESUME:
			skynet->active_filetransfers_up();
			skynet->emit_IncomingFileResumed(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_CANCEL:
			skynet->active_filetransfers_down();
			skynet->emit_IncomingFileCancelled(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_IOERROR:
			skynet->active_filetransfers_down();
			skynet->emit_IncomingFileIoError(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;
		}
	}
	else if (sender_cmd == PICA_PROTO_FILECONTROL_VOID && receiver_cmd != PICA_PROTO_FILECONTROL_VOID)
	{
		switch(receiver_cmd)
		{
		case PICA_PROTO_FILECONTROL_PAUSE:
			skynet->active_filetransfers_down();
			skynet->emit_OutgoingFilePaused(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_RESUME:
			skynet->active_filetransfers_up();
			skynet->emit_OutgoingFileResumed(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_CANCEL:
			skynet->active_filetransfers_down();
			skynet->emit_OutgoingFileCancelled(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_IOERROR:
			skynet->active_filetransfers_down();
			skynet->emit_OutgoingFileIoError(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;
		}
	}
}

void SkyNet::file_finished(const unsigned char *peer_id, int sending)
{
	qDebug() << "file_finished callback\n";

	skynet->active_filetransfers_down();

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

void SkyNet::direct_c2c_established_cb(const unsigned char *peer_id, const char *ciphersuitename)
{
	skynet->emit_ConnectionStatusUpdated(QByteArray((const char*)peer_id, PICA_ID_SIZE), QString("🔐: %1 direct c2c").arg(ciphersuitename));
}

void SkyNet::incoming_call_cb(const unsigned char *peer_id)
{
	skynet->emit_IncomingCall(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::call_picked_up_cb(const unsigned char *peer_id)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);

	skynet->call_active_set(true, peer);
	skynet->emit_CallAccepted(peer);
}

void SkyNet::call_rejected_cb(const unsigned char *peer_id)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);

	skynet->call_active_set(false, peer);
	skynet->emit_CallRejected(peer);
}

void SkyNet::call_hangup_cb(const unsigned char *peer_id)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);

	skynet->call_active_set(false, peer);
	skynet->emit_CallHungup(peer);
}

void SkyNet::call_audio_params_cb(const unsigned char *peer_id, const char *codec, uint16_t sample_rate)
{
	skynet->emit_IncomingAudioParams(QByteArray((const char*)peer_id, PICA_ID_SIZE), QString::fromLatin1(codec), sample_rate);
}

void SkyNet::call_video_params_cb(const unsigned char *peer_id, const char *codec, uint16_t width, uint16_t height)
{
	skynet->emit_IncomingVideoParams(QByteArray((const char*)peer_id, PICA_ID_SIZE), QString::fromLatin1(codec), width, height);
}

void SkyNet::call_audio_packet_cb(const unsigned char *peer_id, uint16_t seq_num, uint32_t timestamp, uint16_t size, const char *pkt_data)
{
	skynet->emit_IncomingAudioPacket(QByteArray((const char*)peer_id, PICA_ID_SIZE), seq_num, timestamp, QByteArray(pkt_data, size));
}

void SkyNet::call_video_packet_cb(const unsigned char *peer_id, uint16_t seq_num, uint32_t timestamp, uint16_t size, const char *pkt_data)
{
	skynet->emit_IncomingVideoPacket(QByteArray((const char*)peer_id, PICA_ID_SIZE), seq_num, timestamp, QByteArray(pkt_data, size));
}

void SkyNet::call_media_transport_cb(const unsigned char *peer_id, int transport, const char *ciphersuitename, unsigned int max_payload)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);
	bool direct_udp = (transport == PICA_CALL_TRANSPORT_MEDIAC2C);

	skynet->emit_ConnectionStatusUpdated(peer, direct_udp
	                                     ? QString("🔐: %1 media").arg(ciphersuitename)
	                                     : QString(tr("call media over the c2c connection")));

	skynet->emit_CallMediaTransportChanged(peer, direct_udp,
	                                       ciphersuitename ? QString::fromLatin1(ciphersuitename) : QString(),
	                                       max_payload);
}
