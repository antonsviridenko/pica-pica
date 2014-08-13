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
    QWidget(0), peer_id_(peer_id), hist(config_dbname, Accounts::GetCurrentAccount().id)
{
    QVBoxLayout *layout;
    QString title;



    {
        Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);
        peer_name_ = ct.GetContactName(peer_id_);

        title = peer_name_;

        if (peer_name_.isEmpty())
            peer_name_ = peer_id_.toBase64();

        QList<Contacts::ContactRecord>  L = ct.GetContacts(Contacts::temporary);
        addcontactquestion = false;
        for (int i = 0; i < L.size(); i++)
        {
            if (L[i].id == peer_id_)
            {
            addcontactquestion = true;
            break;
            }
        }

    }
    {
        Accounts ac(config_dbname);
        my_name_ = ac.GetName(Accounts::GetCurrentAccount().id);

        if (my_name_.isEmpty())
            my_name_ = Accounts::GetCurrentAccount().id.toBase64();
    }

    title = QObject::tr("Chat with ") + title + " - " + peer_id_.toBase64();
    setWindowTitle(title);

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

    if (addcontactquestion)
    {
        QHBoxLayout *hl = new QHBoxLayout();

        lbAddCtQuestion = new QLabel(tr("Do you want to add \"%1\" to your contact list?").arg(peer_name_));
        btAddCtYes = new QPushButton(tr("Yes"));
        btAddCtNo = new QPushButton(tr("No"));
        btAddCtBlacklist = new QPushButton(tr("Add to blacklist"));

        layout->insertWidget(2, lbAddCtQuestion, Qt::AlignLeft);

        hl->addWidget(btAddCtYes);
        hl->addWidget(btAddCtNo);
        hl->addWidget(btAddCtBlacklist);

        layout->insertLayout(3, hl);

        connect(btAddCtYes, SIGNAL(clicked()), this, SLOT(addct_yes()));
        connect(btAddCtNo, SIGNAL(clicked()), this, SLOT(addct_no()));
        connect(btAddCtBlacklist, SIGNAL(clicked()), this, SLOT(addct_blacklist()));
    }

    setLayout(layout);

    setWindowIcon(picapica_ico_sit);

    QWidget::setAttribute(Qt::WA_DeleteOnClose);
    QWidget::setAttribute(Qt::WA_QuitOnClose, false);

    menu = new QMenuBar(this);
    hist24h = new QAction(tr("Last 24 &Hours"), this);
    hist1week = new QAction(tr("Last &Week"), this);
    histAll = new QAction(tr("&All"), this);

    {
        QMenu *m;
        m = menu->addMenu(tr("&History"));
        m = m->addMenu(tr("Show History"));
        m->addAction(hist24h);
        m->addAction(hist1week);
        m->addAction(histAll);
    }
    layout->insertSpacing(0, menu->size().height());

    connect(hist24h, SIGNAL(triggered()), this, SLOT(show_history24h()));
    connect(hist1week, SIGNAL(triggered()), this, SLOT(show_history1w()));
    connect(histAll,SIGNAL(triggered()), this, SLOT(show_historyAll()));

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
    if (undelivered_msgs.isEmpty())
    {
    // undelivered_msgs can be empty if new chatwindow instance was created
    // and undelivered message from previous chat session is delivered
    // after that.

        QByteArray my_id = Accounts::GetCurrentAccount().id;
        QMap<QByteArray, QList<QString> > undl = hist.GetUndeliveredMessages();

        if (!undl[my_id].isEmpty())
            put_message(undl[my_id].first(), my_id , true);
    }

    if (!undelivered_msgs.isEmpty())
    {
        QTextCursor c(chatw->document());
        c.setPosition(undelivered_msgs.first());
        c.deleteChar();
        c.insertText("+");
        undelivered_msgs.removeFirst();
    }

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

bool ChatWindow::isEmptyMessage(const QString &msg) const
{
    int size = msg.size();
    for (int i = 0; i < size; ++i) {
        if (!msg.at(i).isSpace()) {
            return false;
        }
    }
    return true;
}

void ChatWindow::send_message()
{
    QString message = sendtextw->toPlainText();
    if (!isEmptyMessage(message)) {
        put_message(message, Accounts::GetCurrentAccount().id, true);
        hist.Add(peer_id_, message, true);

        if (!hist.isOK())
        {
            QMessageBox mbx;
            mbx.setText(hist.GetLastError());
            mbx.exec();
        }

        emit msg_input(message, this);

        sendtextw->clear();
    }
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

void ChatWindow::addct_yes()
{
    Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);

    ct.Add(peer_id_, Contacts::regular);
    addct_removequestion();

    emit contacts_update();
}

void ChatWindow::addct_no()
{
    addct_removequestion();
}

void ChatWindow::addct_blacklist()
{
    Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);

    ct.Add(peer_id_, Contacts::blacklisted);
    addct_removequestion();
}

void ChatWindow::addct_removequestion()
{
    if (addcontactquestion)
    {
    delete btAddCtYes;
    delete btAddCtNo;
    delete btAddCtBlacklist;
    delete lbAddCtQuestion;
    addcontactquestion = false;
    }
}
