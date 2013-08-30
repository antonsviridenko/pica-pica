#include "chatwindow.h"
#include "globals.h"
#include "contacts.h"
#include "accounts.h"
#include <QVBoxLayout>
#include <QDateTime>
#include <QAction>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDateTime>
#include <QMessageBox>

ChatWindow::ChatWindow(QByteArray peer_id) :
    QWidget(0), peer_id_(peer_id), hist(config_dbname, QByteArray((const char*)account_id, PICA_ID_SIZE))
{
    QVBoxLayout *layout;

    layout = new QVBoxLayout;

    chatw = new QTextEdit(this);
    sendtextw = new TextSend(this);

    connect(sendtextw, SIGNAL(send_pressed()), this, SLOT(send_message()));

    sendtextw->setMinimumHeight(48);
    sendtextw->setMinimumWidth(240);
    sendtextw->setAcceptRichText(false);

    chatw->setReadOnly(true);

    layout->addWidget(chatw, 3);
    layout->addWidget(sendtextw, 1, Qt::AlignBottom);

    setLayout(layout);

    setWindowTitle(peer_id.toBase64());
    setWindowIcon(picapica_ico_sit);

    QWidget::setAttribute(Qt::WA_DeleteOnClose);
    QWidget::setAttribute(Qt::WA_QuitOnClose, false);

    menu = new QMenuBar(this);
    hist24h = new QAction(tr("Last 24 &Hours"), this);
    hist1week = new QAction(tr("Last &Week"), this);
    histAll = new QAction(tr("&All"), this);

    connect(hist24h, SIGNAL(triggered()), this, SLOT(show_history24h()));
    connect(hist1week, SIGNAL(triggered()), this, SLOT(show_history1w()));
    connect(histAll,SIGNAL(triggered()), this, SLOT(show_historyAll()));

    {
        QMenu *m;
        m = menu->addMenu(tr("&History"));
        m = m->addMenu(tr("Show History"));
        m->addAction(hist24h);
        m->addAction(hist1week);
        m->addAction(histAll);
    }

    layout->insertSpacing(0, menu->size().height());

    {
        Contacts ct(config_dbname, QByteArray((const char*)account_id, PICA_ID_SIZE));
        peer_name_ = ct.GetContactName(peer_id_);

        if (peer_name_.isEmpty())
            peer_name_ = peer_id_.toBase64();

    }
    {
        Accounts ac(config_dbname);
        my_name_ = ac.GetName(QByteArray((const char*)account_id, PICA_ID_SIZE));

        if (my_name_.isEmpty())
            my_name_ = QByteArray((const char*)account_id, PICA_ID_SIZE).toBase64();
    }

}

void ChatWindow::put_message(QString msg, QByteArray id, bool is_me)
{
    QString color, name;
    QTextCharFormat fmt;
    int pos;

    if (is_me)
    {
        color = "#0000ff";
        name = my_name_;
    }
    else
    {
        color = "#ff0000";

        if (id == peer_id_)
            name = peer_name_;
        else
            name = id.toBase64();
    }

    pos = draw_message(msg, name, QDateTime::currentDateTime().toString(), color, false);

    if (is_me)
        undelivered_msgs.append(pos);
}

int ChatWindow::draw_message(QString msg, QString nickname, QString datetime, QString color, bool is_delivered)
{
    QTextCharFormat fmt;
    QString delivered_mark;
    int ret;

    if (is_delivered)
        delivered_mark = "+";
    else
        delivered_mark = "&nbsp;";

    QTextCursor c(chatw->document());
    c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);

    ret = c.position();

    chatw->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
    fmt = chatw->currentCharFormat();
    chatw->insertHtml(
                      QString("<b><font color=%1>%2(%3) %4 : </font></b>").
                      arg(color).
                      arg(delivered_mark).
                      arg(datetime).
                      arg(nickname)
                      );
    chatw->setCurrentCharFormat(fmt);
    chatw->insertPlainText(msg + '\n');

    chatw->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);

    return ret;
}

void ChatWindow::print_history(QList<History::HistoryRecord> H)
{
    chatw->clear();

    while(!H.isEmpty())
    {
        History::HistoryRecord r = H.takeFirst();
        QString color, name;
        QDateTime dt;

        if (r.is_me)
        {
            color = "#0000ff";
            name = my_name_;
        }
        else
        {
            color = "#ff0000";

            if (r.peer_id == peer_id_)
                name = peer_name_;
            else
                name = r.peer_id.toBase64();
        }

        dt.setTime_t(r.timestamp);
        //put_message(r.message, r.peer_id, r.is_me);
        draw_message(r.message, name, dt.toString(), color, r.is_delivered);
    }
}

void ChatWindow::msg_from_peer(QString msg)
{
    if (isMinimized())
        showNormal();

    put_message(msg, peer_id_, false);
    hist.Add(peer_id_, msg, false);
}

void ChatWindow::msg_delivered()
{
    QTextCursor c(chatw->document());
    c.setPosition(undelivered_msgs.first());
    c.deleteChar();
    c.insertText("+");
    undelivered_msgs.removeFirst();

    hist.SetDelivered(peer_id_);
}

void ChatWindow::msg_informational(QString text)
{
  QString color;
  QTextCharFormat fmt;

  color = "#303030";

  fmt = chatw->currentCharFormat();

  chatw->insertHtml(
        QString("<i><font color=%1>(%2)</font></i>").
        arg(color).
        arg(text)
        );
  chatw->insertPlainText("\n");

  chatw->setCurrentCharFormat(fmt);
  chatw->moveCursor(QTextCursor::End, QTextCursor::KeepAnchor);
}

void ChatWindow::send_message()
{
    put_message(sendtextw->toPlainText(), QByteArray((const char*)account_id, PICA_ID_SIZE), true);
    hist.Add(peer_id_, sendtextw->toPlainText(), true);

    if (!hist.isOK())
    {
        QMessageBox mbx;
        mbx.setText(hist.GetLastError());
        mbx.exec();
    }

    emit msg_input(sendtextw->toPlainText(), this);

    sendtextw->clear();
}

void ChatWindow::closeEvent(QCloseEvent *e)
{
    emit chatwindow_close(this);

    e->accept();
}

TextSend::TextSend(QWidget *parent) :
    QTextEdit(parent)
{

}

void TextSend::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return && e->modifiers() == Qt::NoModifier )
    {
        emit send_pressed();
        return;
    }

    QTextEdit::keyPressEvent(e);
}

void ChatWindow::show_history24h()
{
   print_history(hist.GetMessages(peer_id_, QDateTime::currentDateTime().addDays(-1).toTime_t(),
                         QDateTime::currentDateTime().toTime_t()));
}

void ChatWindow::show_history1w()
{
    print_history(hist.GetMessages(peer_id_, QDateTime::currentDateTime().addDays(-7).toTime_t(),
                          QDateTime::currentDateTime().toTime_t()));
}

void ChatWindow::show_historyAll()
{
    print_history(hist.GetMessages(peer_id_, 0, QDateTime::currentDateTime().toTime_t()));
}

