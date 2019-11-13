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

signals:

public slots:
	void msg_to_peer(QString msg, ChatWindow *sender_window);
	void msg_from_peer(QByteArray from, QString msg);

	void chatwindow_closed(ChatWindow *sender_window);
	void start_chat(QByteArray peer_id);

	void delivery_failed(QByteArray to, QString msg);
	void delivered(QByteArray to);

	void scary_cert_message(QByteArray peer_id, QString received_cert, QString stored_cert);
	void notification(QString text, bool is_critical);
	void update_status(QByteArray peer_id, QString status);

	void trayicon_dclick();
private:
	QMap<QByteArray, ChatWindow*> chatwindows;
	QMap<QByteArray, QString> connstatus;
	QLinkedList<QByteArray> blinkqueue;

	void create_chatwindow(QByteArray peer_id);

};

#endif // MSGUIROUTER_H
