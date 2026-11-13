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
	connect(skynet, SIGNAL(MessageReceived(QByteArray, QByteArray, QString, QByteArray)), this, SLOT(msg_from_peer(QByteArray, QByteArray, QString, QByteArray)));
	connect(skynet, SIGNAL(UnableToDeliver(QByteArray, QByteArray)), this, SLOT(delivery_failed(QByteArray, QByteArray)));
	connect(skynet, SIGNAL(Delivered(QByteArray, QByteArray)), this, SLOT(delivered(QByteArray, QByteArray)));
	connect(skynet, SIGNAL(HistorySynced(QByteArray, int)), this, SLOT(history_synced(QByteArray, int)));
	connect(skynet, SIGNAL(GroupChanged(QByteArray, QList<QByteArray>, QList<QByteArray>)), this, SLOT(group_changed(QByteArray, QList<QByteArray>, QList<QByteArray>)));
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
	bool was_connected = is_connected(peer_id);

	connstatus[peer_id] = status;

	if (chatwindows.contains(peer_id))
		chatwindows[peer_id]->update_status(status);

	if (was_connected != is_connected(peer_id))
		emit conversations_changed();
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
	connect(cw, SIGNAL(conversation_read()), this, SIGNAL(conversations_changed()));

	cw->show();
}

void MsgUIRouter::msg_from_peer(QByteArray from, QByteArray author, QString msg, QByteArray msg_id)
{
	Sound::play(snd_newmessage);

	if ( ! chatwindows.contains(from))
	{
		create_chatwindow(from);
	}

	chatwindows[from]->msg_from_peer(author, msg, msg_id);
	emit conversations_changed();

	if (!chatwindows[from]->isActiveWindow())
	{
		systray->ShowMessage(chatwindows[from]->getPeerName(),
		                     (author == from ? QString() : chatwindows[from]->name_of(author) + ": ") + msg.left(100));

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

void MsgUIRouter::delivery_failed(QByteArray conv, QByteArray msg_id)
{
	if (chatwindows.contains(conv))
		chatwindows[conv]->msg_failed(msg_id);
}

void MsgUIRouter::transfer_note(QByteArray peer_id, QString text)
{
	// shortcut: shown only in an open chat and not stored, add a history record kind if transfers should be kept
	if (chatwindows.contains(peer_id))
		chatwindows[peer_id]->msg_informational(text);
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

	emit conversations_changed();
}

void MsgUIRouter::group_changed(QByteArray group_id, QList<QByteArray> added, QList<QByteArray> removed)
{
	if (!chatwindows.contains(group_id))
		return;

	ChatWindow *cw = chatwindows[group_id];

	for (int i = 0; i < added.size(); i++)
		cw->msg_informational(tr("%1 was added to the group").arg(cw->name_of(added[i])));

	for (int i = 0; i < removed.size(); i++)
		cw->msg_informational(tr("%1 is no longer in the group").arg(cw->name_of(removed[i])));
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
