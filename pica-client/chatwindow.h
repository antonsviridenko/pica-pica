#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QWidget>
#include <QTextEdit>
#include <QList>
#include <QMenuBar>
#include <QAction>
#include <QMenu>
#include "history.h"

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
    History hist;
    QMenuBar *menu;
    QAction *hist24h;
    QAction *hist1week;
    QAction *histAll;
    QString peer_name_;
    QString my_name_;

    void put_message(QString msg, quint32 id, bool is_me);
    int draw_message(QString msg, QString nickname, QString datetime, QString color, bool is_delivered);
    void print_history(QList<History::HistoryRecord> H);

    void closeEvent(QCloseEvent *e);

private slots:
    void send_message();

    void show_history24h();
    void show_history1w();
    void show_historyAll();
};

#endif // CHATWINDOW_H
