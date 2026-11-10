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
#include "msguirouter.h"
#include "dialogs/forgedcertdialog.h"
#include "history.h"
#include "mainwindow.h"
#include "accountswindow.h"
#include "picasystray.h"
#include "globals.h"
#include "sound.h"


MsgUIRouter::MsgUIRouter(QObject *parent) :
	QObject(parent)
{
	connect(skynet, SIGNAL(MessageReceived(QByteArray, QByteArray, QString)), this, SLOT(msg_from_peer(QByteArray, QByteArray, QString)));
	connect(skynet, SIGNAL(UnableToDeliver(QByteArray, QString)), this, SLOT(delivery_failed(QByteArray, QString)));
	connect(skynet, SIGNAL(Delivered(QByteArray, QByteArray)), this, SLOT(delivered(QByteArray, QByteArray)));
	connect(skynet, SIGNAL(HistorySynced(QByteArray, int)), this, SLOT(history_synced(QByteArray, int)));
	connect(skynet, SIGNAL(CertificateForged(QByteArray, QString, QString)), this, SLOT(scary_cert_message(QByteArray, QString, QString)));
	connect(skynet, SIGNAL(StatusMsg(QString, bool)), this, SLOT(notification(QString, bool)));
	connect(systray, SIGNAL(doubleclicked()), this, SLOT(trayicon_dclick()));
	connect(skynet, SIGNAL(ConnectionStatusUpdated(QByteArray,QString)), this, SLOT(update_status(QByteArray,QString)));

}

void MsgUIRouter::trayicon_dclick()
{
	if (mainwindow == NULL)
	{
		accwindow->showNormal();
		accwindow->activateWindow();
		accwindow->raise();
		return;
	}


	if (blinkqueue.isEmpty())
	{
		mainwindow->showNormal();
		mainwindow->activateWindow();
		mainwindow->raise();
	}
	else
	{
		QByteArray blinker_id = blinkqueue.takeFirst();

		if (chatwindows.contains(blinker_id))
		{
			chatwindows[blinker_id]->showNormal();
			chatwindows[blinker_id]->activateWindow();
			chatwindows[blinker_id]->raise();
		}

		if (blinkqueue.isEmpty())
		{
			systray->StopBlinking();
		}
	}
}

void MsgUIRouter::update_status(QByteArray peer_id, QString status)
{
	connstatus[peer_id] = status;

	if (chatwindows.contains(peer_id))
		chatwindows[peer_id]->update_status(status);
}

void MsgUIRouter::create_chatwindow(QByteArray peer_id)
{
	ChatWindow *cw;

	if (!connstatus.contains(peer_id))
		connstatus[peer_id] = QString(tr("Disconnected"));

	cw = new ChatWindow(peer_id, connstatus[peer_id]);

	chatwindows[peer_id] = cw;

	connect(cw, SIGNAL(msg_input(QString, QByteArray, ChatWindow*)), this, SLOT(msg_to_peer(QString, QByteArray, ChatWindow*)));
	connect(cw, SIGNAL(chatwindow_close(ChatWindow*)), this, SLOT(chatwindow_closed(ChatWindow*)));
	connect(cw, SIGNAL(contacts_update()), skynet, SIGNAL(ContactsUpdated()));
	connect(skynet, SIGNAL(ContactsUpdated()), cw, SLOT(set_peer_name()));

	cw->show();
}

void MsgUIRouter::msg_from_peer(QByteArray from, QByteArray author, QString msg)
{
	Sound::play(snd_newmessage);

	if ( ! chatwindows.contains(from))
	{
		create_chatwindow(from);
	}

	chatwindows[from]->msg_from_peer(author, msg);

	if (!chatwindows[from]->isActiveWindow())
	{
		if (blinkqueue.isEmpty())
		{
			systray->StartBlinking();
			blinkqueue.append(from);
		}
		else if (!blinkqueue.contains(from))
		{
			blinkqueue.append(from);
		}

	}
}

void MsgUIRouter::msg_to_peer(QString msg, QByteArray msg_id, ChatWindow *sender_window)
{
	skynet->SendMessage(sender_window->getPeerId(), msg_id, msg, sender_window->isGroup());

	if (!blinkqueue.isEmpty())
	{
		blinkqueue.removeAll(sender_window->getPeerId());
		if (blinkqueue.isEmpty())
		{
			systray->StopBlinking();
		}
	}
}

void MsgUIRouter::start_chat(QByteArray peer_id)
{
	if ( ! chatwindows.contains(peer_id))
	{
		create_chatwindow(peer_id);
	}
	else
	{
		chatwindows[peer_id]->showNormal();
		chatwindows[peer_id]->activateWindow();
		chatwindows[peer_id]->raise();
	}
}

void MsgUIRouter::chatwindow_closed(ChatWindow *sender_window)
{
	chatwindows.remove(sender_window->getPeerId());
}

void MsgUIRouter::delivery_failed(QByteArray to, QString msg)
{
//  if (chatwindows.contains(to))
//  {
//    chatwindows[to]->msg_informational(tr("failed to deliver"));
//  }
}

void MsgUIRouter::delivered(QByteArray conv, QByteArray msg_id)
{
	if (chatwindows.contains(conv))
		chatwindows[conv]->msg_delivered(msg_id);
}

void MsgUIRouter::history_synced(QByteArray conv, int count)
{
	if (chatwindows.contains(conv))
		chatwindows[conv]->msg_informational(tr("%1 earlier message(s) received from a contact's copy of this chat, see History").arg(count));
}

void MsgUIRouter::scary_cert_message(QByteArray peer_id, QString received_cert, QString stored_cert)
{
	ForgedCertDialog fcd(peer_id, received_cert, stored_cert);
	fcd.exec();
}

void MsgUIRouter::notification(QString text, bool is_critical)
{
	if (mainwindow)
		mainwindow->AddNotification(text, is_critical);
}
