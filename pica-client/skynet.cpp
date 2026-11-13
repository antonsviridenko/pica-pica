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
#include "openssllib.h"
//#include <QMessageBox> //debug
//#include "dialogs/forgedcertdialog.h"
#include <QDebug>
#include "history.h"
#include "dhparam.h"
#include "askpassword.h"
#include "settings.h"
#include "../PICA_netconf.h"
#include <time.h>
#include <QUuid>

// Payloads of PICA_PROTO_EXTMSG: a type octet followed by QDataStream (Qt_5_0) fields.
// Lists are written as a quint32 count plus items by hand, because QDataStream's
// own container reader reserves memory for whatever count the peer sends.
enum ExtType
{
	EXT_MSG = 1,      // msg_id, group_id (empty: one-to-one), timestamp, text (UTF-8), prev_hash, sig
	EXT_ACK = 2,      // msg_id or group_id
	EXT_GROUPINFO = 3,// id, owner, version, name, members, sig
	EXT_SYNCREQ = 4,  // group_id (empty: one-to-one), since, IDs the requester already has
	EXT_SYNCDATA = 5  // group_id, last chunk flag, records: msg_id, author, timestamp, text, prev_hash, sig
};

static const quint32 ext_max_list = 4096;
static const int ext_chunk_size = 60000;
static const int msg_id_size = 16;
static const quint32 sync_margin = 86400; // allows for clocks that are off by up to a day

static void write_list(QDataStream &ds, const QList<QByteArray> &L)
{
	ds << (quint32)L.size();

	for (int i = 0; i < L.size(); i++)
		ds << L[i];
}

static bool read_list(QDataStream &ds, QList<QByteArray> *L, int item_size)
{
	quint32 n = 0;

	ds >> n;

	if (n > ext_max_list)
		return false;

	for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; i++)
	{
		QByteArray b;
		ds >> b;

		if (b.size() != item_size)
			return false;

		*L << b;
	}

	return ds.status() == QDataStream::Ok;
}

static void write_groupinfo(QDataStream &ds, const Groups::GroupRecord &g)
{
	ds << (quint8)EXT_GROUPINFO << g.id << g.owner << g.version << g.name;
	write_list(ds, g.members);
	ds << g.sig;
}

SkyNet::SkyNet()
	: nodes(nullptr), QObject(0), active_filetransfers(0)
{
	self_aware = false;
	call_waiting_c2c_connect = false;

	qRegisterMetaType<Accounts::AccountRecord>("Accounts::AccountRecord");
	qRegisterMetaType<QList<QByteArray> >("QList<QByteArray>");

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
		call_media_transport_cb,
		c2c_ext_cb,
		ext_msg_cb
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

void SkyNet::active_filetransfers_up(QByteArray peer, bool sending)
{
	QSet<QByteArray> &counted = sending ? counted_ft_out : counted_ft_in;

	// Idempotent: a transfer already counted (e.g. a second RESUME) must not
	// be counted twice.
	if (counted.contains(peer))
		return;

	counted.insert(peer);
	active_filetransfers++;

	if (active_filetransfers > 0 && file_transfer_timer_id == 0)
	{
		file_transfer_timer_id = startTimer(0);
	}
}

void SkyNet::active_filetransfers_down(QByteArray peer, bool sending)
{
	QSet<QByteArray> &counted = sending ? counted_ft_out : counted_ft_in;

	// Idempotent: only a transfer that was actually counted can be removed. A
	// cancel/ioerror for one that was never accepted (WAITACCEPT) or already
	// paused is a no-op, rather than wrapping the unsigned counter past zero.
	if (!counted.contains(peer))
		return;

	counted.remove(peer);
	active_filetransfers--;

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
	counted_ft_in.clear();
	counted_ft_out.clear();
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

		QString name = OpenSSLLib::NameFromCertString(cert_pem);

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
	if (self_aware)
	{
		History h(config_dbname, me(), network_sql_connection_name);
		time_t t = time(NULL);

		// shortcut: outbox items only an EXTMSG peer can take keep reconnecting to an older client (with backoff)
		QList<QByteArray> c2c_peer_ids = (h.OutboxPeers().toSet() + sndfilequeues.keys().toSet()).toList();

		if (c2c_peer_ids.isEmpty())
			return;

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
	{
		flush_ext_waiting();
		reconnect_c2c();
	}

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

	sent_on_chan.clear();
	ext_wait.clear();

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
			active_filetransfers_up(from, false);
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
		else
			active_filetransfers_down(peer_id, pause_sending);
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
		else
			active_filetransfers_up(peer_id, resume_sending);
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
		else
			active_filetransfers_down(peer_id, cancel_sending);
	}

}

void SkyNet::SendMessage(QByteArray conv, QByteArray msg_id, QString msg, bool is_group)
{
	QMetaObject::invokeMethod(this, "do_SendMessage", Qt::AutoConnection,
	                           Q_ARG(QByteArray, conv), Q_ARG(QByteArray, msg_id),
	                           Q_ARG(QString, msg), Q_ARG(bool, is_group));
}

void SkyNet::do_SendMessage(QByteArray conv, QByteArray msg_id, QString msg, bool is_group)
{
	History h(config_dbname, me(), network_sql_connection_name);
	History::HistoryRecord r;
	QList<QByteArray> recipients;

	if (is_group)
	{
		Groups::GroupRecord g;

		if (!Groups(config_dbname, me(), network_sql_connection_name).Get(conv, &g) || !g.members.contains(me()))
		{
			emit UnableToDeliver(conv, msg);
			return;
		}

		recipients = g.members;
		recipients.removeAll(me());
	}
	else
		recipients << conv;

	r.peer_id = is_group ? me() : conv;
	r.is_me = true;
	r.is_delivered = recipients.isEmpty();
	r.timestamp = (quint32)time(NULL);
	r.message = msg;
	r.msg_id = msg_id;
	r.group_id = is_group ? conv : QByteArray();
	r.prev_hash = h.LastHash(conv, is_group);
	r.sig = sign(h.SignedData(r));

	if (!h.Add(r))
	{
		emit UnableToDeliver(conv, msg);
		return;
	}

	for (int i = 0; i < recipients.size(); i++)
	{
		h.OutboxAdd(recipients[i], msg_id, History::OutboxMessage);
		deliver(recipients[i]);
	}
}

void SkyNet::deliver(QByteArray to)
{
	if (find_active_chan(to))
		flush_queues(to);
	else if (self_aware && active_nodelink && !filter_existing_chans(QList<QByteArray>() << to).isEmpty())
	{
		struct PICA_c2c *chan = NULL;

		PICA_new_c2c(active_nodelink, (const unsigned char*)to.constData(), NULL, &chan);
	}
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

	if (self_aware && !ext_wait.contains(to))
	{
		if ((chan = find_active_chan(to)))
		{
			/////// messages
			History h(config_dbname, me(), network_sql_connection_name);
			QList<History::OutboxItem> outbox = h.Outbox(to);

			for (int i = 0; i < outbox.size(); i++)
			{
				const QByteArray &id = outbox[i].msg_id;
				QByteArray payload;
				QDataStream ds(&payload, QIODevice::WriteOnly);

				ds.setVersion(QDataStream::Qt_5_0);

				if (sent_on_chan[to].contains(id))
					continue;

				if (outbox[i].kind == History::OutboxGroupInfo)
				{
					Groups::GroupRecord g;

					if (!chan->peer_ext)
						continue;

					if (!Groups(config_dbname, me(), network_sql_connection_name).Get(id, &g))
					{
						h.OutboxRemove(to, id);
						continue;
					}

					write_groupinfo(ds, g);
					ret = PICA_send_ext(chan, payload.constData(), payload.size());
				}
				else
				{
					History::HistoryRecord r;

					if (!h.Get(id, &r))
					{
						h.OutboxRemove(to, id);
						continue;
					}

					if (chan->peer_ext)
					{
						ds << (quint8)EXT_MSG << r.msg_id << r.group_id << r.timestamp << r.message.toUtf8() << r.prev_hash << r.sig;
						ret = PICA_send_ext(chan, payload.constData(), payload.size());
					}
					else if (r.group_id.isEmpty())
						ret = PICA_send_msg(chan, r.message.toUtf8().data(), r.message.toUtf8().size());
					else
						continue; // group messages wait until the peer runs a client that supports them
				}

				if (ret != PICA_OK)
				{
					qDebug() << "failed to send outbox item to" << to.toBase64() << "ret =" << ret;

					if (ret != PICA_ERRMSGSIZE)
						break;

					h.OutboxRemove(to, id, false);
					emit StatusMsg(tr("A message is too long to be sent and was dropped"), true);
					continue;
				}

				sent_on_chan[to].insert(id);
			}


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
	skynet->store_received(QByteArray((const char*)peer_id, PICA_ID_SIZE), QByteArray(msgbuf, nb));
}

void SkyNet::msgok_cb(const unsigned char *peer_id)
{
	skynet->legacy_delivered(QByteArray((const char*)peer_id, PICA_ID_SIZE));
}

void SkyNet::c2c_established_cb(const unsigned char *peer_id, const char *ciphersuitename)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);
	skynet->reset_c2c_reconnect_timeout(peer);
	skynet->emit_ConnectionStatusUpdated(peer, QString("🔐: %1 c2c").arg(ciphersuitename));
	skynet->sent_on_chan.remove(peer);

	struct PICA_c2c *chan = skynet->find_active_chan(peer);

	if (chan && !chan->outgoing && !chan->peer_ext)
	{
		// an EXTHELLO may follow, see c2c_ext_cb()
		skynet->ext_wait[peer] = time(NULL);
	}
	else
	{
		skynet->flush_queues(peer);

		if (chan && chan->peer_ext)
			skynet->start_sync(peer);
	}

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
	skynet->sent_on_chan.remove(QByteArray((const char*)peer_id, PICA_ID_SIZE));
	skynet->ext_wait.remove(QByteArray((const char*)peer_id, PICA_ID_SIZE));
	/* transfers carried by this connection are over */
	skynet->active_filetransfers_down(QByteArray((const char*)peer_id, PICA_ID_SIZE), true);
	skynet->active_filetransfers_down(QByteArray((const char*)peer_id, PICA_ID_SIZE), false);
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

	skynet->active_filetransfers_up(QByteArray((const char *)peer_id, PICA_ID_SIZE), true);
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
			skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), false);
			skynet->emit_IncomingFilePaused(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_RESUME:
			skynet->active_filetransfers_up(QByteArray((const char *)peer_id, PICA_ID_SIZE), false);
			skynet->emit_IncomingFileResumed(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_CANCEL:
			skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), false);
			skynet->emit_IncomingFileCancelled(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_IOERROR:
			skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), false);
			skynet->emit_IncomingFileIoError(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;
		}
	}
	else if (sender_cmd == PICA_PROTO_FILECONTROL_VOID && receiver_cmd != PICA_PROTO_FILECONTROL_VOID)
	{
		switch(receiver_cmd)
		{
		case PICA_PROTO_FILECONTROL_PAUSE:
			skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), true);
			skynet->emit_OutgoingFilePaused(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_RESUME:
			skynet->active_filetransfers_up(QByteArray((const char *)peer_id, PICA_ID_SIZE), true);
			skynet->emit_OutgoingFileResumed(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_CANCEL:
			skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), true);
			skynet->emit_OutgoingFileCancelled(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;

		case PICA_PROTO_FILECONTROL_IOERROR:
			skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), true);
			skynet->emit_OutgoingFileIoError(QByteArray((const char *)peer_id, PICA_ID_SIZE));
			break;
		}
	}
}

void SkyNet::file_finished(const unsigned char *peer_id, int sending)
{
	qDebug() << "file_finished callback\n";

	skynet->active_filetransfers_down(QByteArray((const char *)peer_id, PICA_ID_SIZE), sending);

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

//EXTMSG

void SkyNet::c2c_ext_cb(const unsigned char *peer_id)
{
	QByteArray peer((const char*)peer_id, PICA_ID_SIZE);

	skynet->ext_wait.remove(peer);
	skynet->flush_queues(peer);
	skynet->start_sync(peer);
}

void SkyNet::ext_msg_cb(const unsigned char *peer_id, const char *buf, unsigned int nb)
{
	skynet->handle_ext(QByteArray((const char*)peer_id, PICA_ID_SIZE), QByteArray(buf, nb));
}

void SkyNet::flush_ext_waiting()
{
	time_t t = time(NULL);
	QList<QByteArray> peers = ext_wait.keys();

	for (int i = 0; i < peers.size(); i++)
	{
		// no EXTHELLO within a couple of seconds: the caller runs an older client
		if (t - ext_wait[peers[i]] >= 2)
		{
			ext_wait.remove(peers[i]);
			flush_queues(peers[i]);
		}
	}
}

QByteArray SkyNet::me() const
{
	return skynet_account.id;
}

QByteArray SkyNet::sign(const QByteArray &data)
{
	unsigned char *sig = NULL;
	size_t siglen = 0;

	if (!acc || PICA_acc_sign(acc, data.constData(), data.size(), &sig, &siglen) != PICA_OK)
	{
		qWarning() << "unable to sign, the message is sent unsigned";
		return QByteArray();
	}

	QByteArray ret((const char*)sig, siglen);
	free(sig);
	return ret;
}

bool SkyNet::send_ext(QByteArray to, const QByteArray &payload)
{
	struct PICA_c2c *chan = find_active_chan(to);

	return chan && PICA_send_ext(chan, payload.constData(), payload.size()) == PICA_OK;
}

void SkyNet::store_received(QByteArray from, const QByteArray &msg_bytes)
{
	History h(config_dbname, me(), network_sql_connection_name);
	History::HistoryRecord r;

	// message from an older client: it has no ID and no signature
	r.peer_id = from;
	r.is_me = false;
	r.is_delivered = true;
	r.timestamp = (quint32)time(NULL);
	r.message = QString::fromUtf8(msg_bytes);
	r.msg_id = QUuid::createUuid().toRfc4122();
	r.prev_hash = h.LastHash(from, false);

	if (h.Add(r))
		emit MessageReceived(from, from, r.message);
}

void SkyNet::legacy_delivered(QByteArray to)
{
	History h(config_dbname, me(), network_sql_connection_name);
	QList<History::OutboxItem> outbox = h.Outbox(to);

	// older clients confirm messages in the order they were sent, without IDs
	for (int i = 0; i < outbox.size(); i++)
	{
		if (outbox[i].kind != History::OutboxMessage || !sent_on_chan[to].contains(outbox[i].msg_id))
			continue;

		sent_on_chan[to].remove(outbox[i].msg_id);

		if (h.OutboxRemove(to, outbox[i].msg_id))
			emit Delivered(to, outbox[i].msg_id);

		break;
	}
}

void SkyNet::handle_ext(QByteArray from, const QByteArray &payload)
{
	QDataStream ds(payload);
	quint8 type = 0;

	ds.setVersion(QDataStream::Qt_5_0);
	ds >> type;

	switch (type)
	{
	case EXT_MSG:
		handle_ext_msg(from, ds);
		break;
	case EXT_ACK:
		handle_ext_ack(from, ds);
		break;
	case EXT_GROUPINFO:
		handle_ext_groupinfo(from, ds);
		break;
	case EXT_SYNCREQ:
		handle_ext_syncreq(from, ds);
		break;
	case EXT_SYNCDATA:
		handle_ext_syncdata(from, ds);
		break;
	default:
		qDebug() << "unknown EXTMSG type" << type << "from" << from.toBase64();
	}
}

static bool is_member(const QByteArray &group_id, const QByteArray &me, const QByteArray &who, Groups::GroupRecord *g)
{
	return Groups(config_dbname, me, network_sql_connection_name).Get(group_id, g)
	       && g->members.contains(me) && g->members.contains(who);
}

void SkyNet::handle_ext_msg(QByteArray from, QDataStream &ds)
{
	History h(config_dbname, me(), network_sql_connection_name);
	History::HistoryRecord r;
	Groups::GroupRecord g;
	QByteArray text;

	ds >> r.msg_id >> r.group_id >> r.timestamp >> text >> r.prev_hash >> r.sig;

	if (ds.status() != QDataStream::Ok || r.msg_id.size() != msg_id_size)
		return;

	// acknowledged even when dropped below, otherwise the sender would keep resending it
	{
		QByteArray ack;
		QDataStream as(&ack, QIODevice::WriteOnly);

		as.setVersion(QDataStream::Qt_5_0);
		as << (quint8)EXT_ACK << r.msg_id;
		send_ext(from, ack);
	}

	if (!r.group_id.isEmpty() && !is_member(r.group_id, me(), from, &g))
		return;

	r.peer_id = from;
	r.is_me = false;
	r.is_delivered = true;
	r.message = QString::fromUtf8(text);

	// the connection already proves who sent it; a signature is required for it to be synced on later
	if (!r.sig.isEmpty() && !h.VerifySignature(r))
	{
		qWarning() << "dropping message with an invalid signature from" << from.toBase64();
		return;
	}

	if (h.Add(r))
		emit MessageReceived(r.group_id.isEmpty() ? from : r.group_id, from, r.message);
}

void SkyNet::handle_ext_ack(QByteArray from, QDataStream &ds)
{
	History h(config_dbname, me(), network_sql_connection_name);
	History::HistoryRecord r;
	QByteArray id;

	ds >> id;

	if (ds.status() != QDataStream::Ok)
		return;

	sent_on_chan[from].remove(id);

	if (h.OutboxRemove(from, id) && h.Get(id, &r))
		emit Delivered(r.group_id.isEmpty() ? r.peer_id : r.group_id, id);
}

void SkyNet::handle_ext_groupinfo(QByteArray from, QDataStream &ds)
{
	History h(config_dbname, me(), network_sql_connection_name);
	Groups gs(config_dbname, me(), network_sql_connection_name);
	Groups::GroupRecord g, stored;
	bool known;

	ds >> g.id >> g.owner >> g.version >> g.name;

	if (!read_list(ds, &g.members, PICA_ID_SIZE))
		return;

	ds >> g.sig;

	if (ds.status() != QDataStream::Ok || g.id.size() != msg_id_size
	        || g.owner.size() != PICA_ID_SIZE || !g.members.contains(g.owner))
		return;

	{
		QByteArray ack;
		QDataStream as(&ack, QIODevice::WriteOnly);

		as.setVersion(QDataStream::Qt_5_0);
		as << (quint8)EXT_ACK << g.id;
		send_ext(from, ack);
	}

	known = gs.Get(g.id, &stored);

	if (known && (stored.owner != g.owner || stored.version >= g.version))
		return;

	// group info can be relayed by any member during sync, so only the owner's signature counts
	QByteArray cert = h.CertOf(g.owner).toLatin1();
	QByteArray data = Groups::SignedData(g);

	if (cert.isEmpty() || !PICA_verify_by_cert(cert.constData(), (const unsigned char*)g.owner.constData(),
	        data.constData(), data.size(), (const unsigned char*)g.sig.constData(), g.sig.size()))
	{
		qWarning() << "dropping group info that is not signed by its owner, received from" << from.toBase64();
		return;
	}

	gs.Store(g);
	emit ContactsUpdated();

	if (!known && g.members.contains(me()) && g.members.contains(from))
		send_syncreq(from, g.id);
}

void SkyNet::start_sync(QByteArray peer)
{
	QList<Groups::GroupRecord> groups = Groups(config_dbname, me(), network_sql_connection_name).GetGroups();

	send_syncreq(peer, QByteArray());

	for (int i = 0; i < groups.size(); i++)
		if (groups[i].members.contains(peer))
			send_syncreq(peer, groups[i].id);
}

void SkyNet::send_syncreq(QByteArray peer, QByteArray group_id)
{
	History h(config_dbname, me(), network_sql_connection_name);
	QByteArray conv = group_id.isEmpty() ? peer : group_id;
	quint32 since = h.LastSync(peer, conv);
	QList<History::HistoryRecord> L;
	QList<QByteArray> have;
	QByteArray payload;
	QDataStream ds(&payload, QIODevice::WriteOnly);

	since = since > sync_margin ? since - sync_margin : 0;
	L = h.GetMessages(conv, !group_id.isEmpty(), since, 0xFFFFFFFF);

	// shortcut: only the newest IDs fit in one request; a gap older than them is not filled,
	// split the request into several if long offline periods turn out to need that
	const int max_have = 2900;

	if (L.size() > max_have)
	{
		L = L.mid(L.size() - max_have);
		since = L.first().timestamp;
	}

	for (int i = 0; i < L.size(); i++)
		have << L[i].msg_id;

	ds.setVersion(QDataStream::Qt_5_0);
	ds << (quint8)EXT_SYNCREQ << group_id << since;
	write_list(ds, have);

	send_ext(peer, payload);
}

void SkyNet::handle_ext_syncreq(QByteArray from, QDataStream &ds)
{
	History h(config_dbname, me(), network_sql_connection_name);
	Groups::GroupRecord g;
	QByteArray group_id;
	quint32 since = 0;
	QList<QByteArray> have_list;
	QSet<QByteArray> have;
	QList<History::HistoryRecord> L;

	ds >> group_id >> since;

	if (ds.status() != QDataStream::Ok || !read_list(ds, &have_list, msg_id_size))
		return;

	if (!group_id.isEmpty() && !is_member(group_id, me(), from, &g))
		return;

	have = have_list.toSet();
	L = h.GetMessages(group_id.isEmpty() ? from : group_id, !group_id.isEmpty(), since, 0xFFFFFFFF);

	QList<QByteArray> records;

	for (int i = 0; i < L.size(); i++)
	{
		// unsigned records (older clients, pre-upgrade history) have a different random ID on
		// each side, so serving them would duplicate what the requester already has
		if (have.contains(L[i].msg_id) || L[i].sig.isEmpty())
			continue;

		QByteArray rec;
		QDataStream rs(&rec, QIODevice::WriteOnly);

		rs.setVersion(QDataStream::Qt_5_0);
		rs << L[i].msg_id << h.Author(L[i]) << L[i].timestamp << L[i].message.toUtf8() << L[i].prev_hash << L[i].sig;
		records << rec;
	}

	// records go out in chunks that fit one EXTMSG; the last chunk is sent even when empty
	int pos = 0;

	do
	{
		QByteArray chunk, body;
		quint32 count = 0;

		while (pos < records.size() && (body.size() + records[pos].size() < ext_chunk_size || count == 0))
		{
			body += records[pos++];
			count++;
		}

		{
			QDataStream cs(&chunk, QIODevice::WriteOnly);
			cs.setVersion(QDataStream::Qt_5_0);
			cs << (quint8)EXT_SYNCDATA << group_id << (bool)(pos >= records.size()) << count;
		}
		chunk += body;

		send_ext(from, chunk);
	}
	while (pos < records.size());

	// a new device of this peer learns which groups it belongs to
	if (group_id.isEmpty())
	{
		QList<Groups::GroupRecord> groups = Groups(config_dbname, me(), network_sql_connection_name).GetGroups();

		for (int i = 0; i < groups.size(); i++)
		{
			if (!groups[i].members.contains(from) || groups[i].sig.isEmpty())
				continue;

			QByteArray payload;
			QDataStream ps(&payload, QIODevice::WriteOnly);

			ps.setVersion(QDataStream::Qt_5_0);
			write_groupinfo(ps, groups[i]);
			send_ext(from, payload);
		}
	}
}

void SkyNet::handle_ext_syncdata(QByteArray from, QDataStream &ds)
{
	History h(config_dbname, me(), network_sql_connection_name);
	Groups::GroupRecord g;
	QByteArray group_id;
	bool last = false;
	quint32 count = 0;
	int added = 0;

	ds >> group_id >> last >> count;

	if (ds.status() != QDataStream::Ok || count > ext_max_list)
		return;

	if (!group_id.isEmpty() && !is_member(group_id, me(), from, &g))
		return;

	for (quint32 i = 0; i < count; i++)
	{
		History::HistoryRecord r;
		QByteArray author, text;

		ds >> r.msg_id >> author >> r.timestamp >> text >> r.prev_hash >> r.sig;

		if (ds.status() != QDataStream::Ok)
			return;

		if (r.msg_id.size() != msg_id_size)
			continue;

		if (group_id.isEmpty() ? (author != me() && author != from) : !g.members.contains(author))
			continue;

		r.is_me = author == me();
		r.peer_id = group_id.isEmpty() ? from : author;
		r.group_id = group_id;
		r.is_delivered = true;
		r.message = QString::fromUtf8(text);

		// the peer could make up anything it serves, so others' messages must carry the author's signature
		// shortcut: messages from before signing existed are accepted only when the peer itself wrote them
		if (r.sig.isEmpty() ? author != from : !h.VerifySignature(r))
			continue;

		if (h.Add(r))
			added++;
	}

	if (added)
		emit HistorySynced(group_id.isEmpty() ? from : group_id, added);

	if (last)
		h.SetLastSync(from, group_id.isEmpty() ? from : group_id, (quint32)time(NULL));
}

void SkyNet::SaveGroup(QByteArray group_id, QString name, QList<QByteArray> members)
{
	QMetaObject::invokeMethod(this, "do_SaveGroup", Qt::AutoConnection,
	                           Q_ARG(QByteArray, group_id), Q_ARG(QString, name),
	                           Q_ARG(QList<QByteArray>, members));
}

void SkyNet::do_SaveGroup(QByteArray group_id, QString name, QList<QByteArray> members)
{
	History h(config_dbname, me(), network_sql_connection_name);
	Groups gs(config_dbname, me(), network_sql_connection_name);
	Groups::GroupRecord g;
	QList<QByteArray> notify;

	if (gs.Get(group_id, &g))
	{
		if (g.owner != me())
		{
			emit StatusMsg(tr("Only the owner of a group can change it"), false);
			return;
		}

		notify = g.members;
		g.version++;
	}
	else
	{
		g.id = group_id;
		g.owner = me();
		g.version = 1;
	}

	g.name = name;
	g.members = (members.toSet() << me()).toList();
	g.sig = sign(Groups::SignedData(g));

	if (g.sig.isEmpty())
	{
		emit StatusMsg(tr("Unable to sign the group information"), true);
		return;
	}

	gs.Store(g);
	emit ContactsUpdated();

	// removed members are told too, so they know they are out
	notify = (notify.toSet() + g.members.toSet()).toList();
	notify.removeAll(me());

	for (int i = 0; i < notify.size(); i++)
	{
		h.OutboxAdd(notify[i], g.id, History::OutboxGroupInfo);
		sent_on_chan[notify[i]].remove(g.id);
		deliver(notify[i]);
	}
}
