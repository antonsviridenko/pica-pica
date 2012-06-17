#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QWidget>
#include <QTextEdit>
#include <QList>

class TextSend : public QTextEdit
{
    Q_OBJECT
public:
    explicit TextSend(QWidget *parent);
signals:
    void send_pressed();
private:
    void keyPressEvent(QKeyEvent *e);

};

class ChatWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ChatWindow(quint32 peer_id);

    quint32 getPeerId() { return peer_id_;};

signals:
    void msg_input(QString msg, ChatWindow *sender_window);
    void chatwindow_close(ChatWindow *sender_window);

public slots:
    void msg_from_peer(QString msg);
    void msg_delivered();
    void msg_informational(QString text);//юзер в оффлайне, набирает сообщение, и т.д. серым шрифтом, курсив


private:
    QTextEdit *chatw;
    TextSend *sendtextw;
    quint32 peer_id_;
    QList<int> undelivered_msgs;


    void put_message(QString msg, quint32 id, bool is_me);

    void closeEvent(QCloseEvent *e);

private slots:
    void send_message();
};

#endif // CHATWINDOW_H
