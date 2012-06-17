#include "chatwindow.h"
#include "globals.h"
#include <QVBoxLayout>
#include <QDateTime>
#include <QAction>
#include <QKeyEvent>
#include <QMessageBox>

ChatWindow::ChatWindow(quint32 peer_id) :
    QWidget(0), peer_id_(peer_id)
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

    setWindowTitle(QString::number(peer_id));
    setWindowIcon(picapica_ico_sit);

    QWidget::setAttribute(Qt::WA_DeleteOnClose);
    QWidget::setAttribute(Qt::WA_QuitOnClose, false);
}

void ChatWindow::put_message(QString msg, quint32 id, bool is_me)
{
    QString color;
    QTextCharFormat fmt;

    if (is_me)
        color = "#0000ff";
    else
        color = "#ff0000";

    QTextCursor c(chatw->document());

    c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);

    if (is_me)
        undelivered_msgs.append(c.position());

    fmt = chatw->currentCharFormat();
    chatw->insertHtml(
                      QString("<b><font color=%1>&nbsp;(%2) %3 : </font></b>").
                      arg(color).
                      arg(QDateTime::currentDateTime().toString()).
                      arg(QString::number(id))
                      );
    chatw->setCurrentCharFormat(fmt);
    chatw->insertPlainText(msg + '\n');

    chatw->moveCursor(QTextCursor::End, QTextCursor::KeepAnchor);
}

void ChatWindow::msg_from_peer(QString msg)
{
    if (isMinimized())
        showNormal();

    put_message(msg, peer_id_, false);
}

void ChatWindow::msg_delivered()
{
    QTextCursor c(chatw->document());
    c.setPosition(undelivered_msgs.first());
    c.deleteChar();
    c.insertText("+");
    undelivered_msgs.removeFirst();
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
    put_message(sendtextw->toPlainText(), account_id, true);

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



