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

	void trayicon_dclick();
private:
	QMap<QByteArray, ChatWindow*> chatwindows;
	QLinkedList<QByteArray> blinkqueue;

	void create_chatwindow(QByteArray peer_id);

};

#endif // MSGUIROUTER_H
