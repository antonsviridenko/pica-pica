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
#ifndef MSGUIROUTER_H
#define MSGUIROUTER_H

#include <QObject>
#include <QLinkedList>
#include <QMap>
#include "chatwindow.h"
#include "skynet.h"
#include "globals.h"

class MsgUIRouter : public QObject
{
	Q_OBJECT
public:
	explicit MsgUIRouter(QObject *parent = 0);

	// There is no presence protocol: a contact counts as reachable while a c2c
	// connection to it is up, which SkyNet reports only as a status text.
	static bool status_connected(const QString &status)
	{
		return status.startsWith(QString::fromUtf8("\xF0\x9F\x94\x90")); // the lock all "connected" statuses begin with
	}
	bool is_connected(QByteArray peer_id)
	{
		return status_connected(connstatus.value(peer_id));
	}

signals:
	void conversations_changed(); // unread counts, last messages or reachability changed

public slots:
	void msg_to_peer(QString msg, QByteArray msg_id, ChatWindow *sender_window);
	void msg_from_peer(QByteArray conv, QByteArray author, QString msg, QByteArray msg_id);
	void history_synced(QByteArray conv, int count);
	void group_changed(QByteArray group_id, QList<QByteArray> added, QList<QByteArray> removed);

	void chatwindow_closed(ChatWindow *sender_window);
	void start_chat(QByteArray peer_id);

	void delivery_failed(QByteArray conv, QByteArray msg_id);
	void delivered(QByteArray conv, QByteArray msg_id);

	void scary_cert_message(QByteArray peer_id, QString received_cert, QString stored_cert);
	void notification(QString text, bool is_critical);
	void update_status(QByteArray peer_id, QString status);
	void transfer_note(QByteArray peer_id, QString text);

	void trayicon_dclick();
private:
	QMap<QByteArray, ChatWindow*> chatwindows;
	QMap<QByteArray, QString> connstatus;
	QLinkedList<QByteArray> blinkqueue;

	void create_chatwindow(QByteArray peer_id);

};

#endif // MSGUIROUTER_H
