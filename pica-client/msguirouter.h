#ifndef MSGUIROUTER_H
#define MSGUIROUTER_H

#include <QObject>
#include <QList>
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
    void msg_from_peer(quint32 from, QString msg);

    void chatwindow_closed(ChatWindow *sender_window);
    void start_chat(quint32 peer_id);

    void delivery_failed(quint32 to, QString msg);
    void delivered(quint32 to);

    void scary_cert_message(quint32 peer_id, QString received_cert, QString stored_cert);
private:
    QMap<quint32,ChatWindow*> chatwindows;

    void create_chatwindow(quint32 peer_id);
    
};

#endif // MSGUIROUTER_H
