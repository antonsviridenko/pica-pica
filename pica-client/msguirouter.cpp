#include "msguirouter.h"
#include "dialogs/forgedcertdialog.h"
#include "history.h"
#include "mainwindow.h"


/*
  этот класс должен обеспечивать связь между приёмом-отправкой сообщений и их отображением в пользовательском интерфейсе.
  Класс SkyNet отвечает только за прием и отправку сообщений, пересылку через сеть и работу с протоколом, ему пофиг,
  как они показываются юзеру. Данный класс должен работать с GUI, например, по приходу нового сообщения (через сигнал от
  класса SkyNet) либо помигать иконкой в трее, либо создать новое окно чата, либо развернуть уже имеющееся, либо помигать
  свернутым окном на таскбаре. Это поведение может настраиваться, настройки хранить в базе и загружать в конструкторе из
  базы sqlite. Например, данный класс должен управлять экземплярами ChatWindow, создавать новые при необходимости либо
  активировать имеющиеся. Также этот класс должен обеспечивать хранение отложенных сообщений (когда собеседник недоступен).
  Можно придумать название получше
 */
MsgUIRouter::MsgUIRouter(QObject *parent) :
    QObject(parent)
{
    connect(skynet, SIGNAL(MessageReceived(quint32,QString)), this, SLOT(msg_from_peer(quint32,QString)));
    connect(skynet, SIGNAL(UnableToDeliver(quint32,QString)), this, SLOT(delivery_failed(quint32,QString)));
    connect(skynet, SIGNAL(Delivered(quint32)), this, SLOT(delivered(quint32)));
    connect(skynet, SIGNAL(CertificateForged(quint32,QString,QString)), this, SLOT(scary_cert_message(quint32,QString,QString)));
    connect(skynet, SIGNAL(ErrMsgFromNode(QString)), this, SLOT(notification(QString)));

}

void MsgUIRouter::create_chatwindow(quint32 peer_id)
{
    ChatWindow *cw;

    cw = new ChatWindow(peer_id);

    chatwindows[peer_id] = cw;

    connect(cw, SIGNAL(msg_input(QString,ChatWindow*)), this, SLOT(msg_to_peer(QString,ChatWindow*)));
    connect(cw, SIGNAL(chatwindow_close(ChatWindow*)), this, SLOT(chatwindow_closed(ChatWindow*)));

    cw->show();
}

void MsgUIRouter::msg_from_peer(quint32 from, QString msg)
{

    if ( ! chatwindows.contains(from))
    {
        create_chatwindow(from);
    }

    chatwindows[from]->msg_from_peer(msg);

}

void MsgUIRouter::msg_to_peer(QString msg, ChatWindow *sender_window)
{
    skynet->SendMessage(sender_window->getPeerId(), msg);
}

void MsgUIRouter::start_chat(quint32 peer_id)
{
    if ( ! chatwindows.contains(peer_id))
    {
        create_chatwindow(peer_id);
    }
    else
    {
        chatwindows[peer_id]->showNormal();
    }
}

void MsgUIRouter::chatwindow_closed(ChatWindow *sender_window)
{
    chatwindows.remove(sender_window->getPeerId());
}

void MsgUIRouter::delivery_failed(quint32 to, QString msg)
{
//  if (chatwindows.contains(to))
//  {
//    chatwindows[to]->msg_informational(tr("failed to deliver"));
//  }
}

void MsgUIRouter::delivered(quint32 to)
{
  if (chatwindows.contains(to))
  {
    chatwindows[to]->msg_delivered();
  }
  else
  {
      History h(config_dbname, account_id);

      h.SetDelivered(to);
  }
}

void MsgUIRouter::scary_cert_message(quint32 peer_id, QString received_cert, QString stored_cert)
{
    ForgedCertDialog fcd(peer_id, received_cert, stored_cert);
    fcd.exec();
}

void MsgUIRouter::notification(QString text)
{
    if (mainwindow)
        mainwindow->AddNotification(text);
}
