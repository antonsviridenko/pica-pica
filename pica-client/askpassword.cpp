#include "askpassword.h"
#include "globals.h"
#include <string.h>
#include <QInputDialog>

QMutex AskPassword::passwd_mutex;
QMap<quint32,QString> AskPassword::idpasswd;

AskPassword::AskPassword(QObject *parent) :
    QObject(parent)
{
    connect(this,SIGNAL(password_requested()),this,SLOT(password_dialog()),Qt::BlockingQueuedConnection);
}

int AskPassword::ask_password_cb(char *buf, int size, int rwflag, void *userdata)
{
    QString pswd;
    quint32 id = *((quint32*)userdata);

    passwd_mutex.lock();

    if (idpasswd.contains(id))
    {
        pswd = idpasswd[id];
    }
    else
    {
        askpassword->emit_sig();
        pswd = idpasswd[id];
    }
    passwd_mutex.unlock();

    strncpy(buf, pswd.toUtf8().constData(), size);
    buf[size - 1] = 0;
    return(strlen(buf));
}

void AskPassword::emit_sig()
{
    emit password_requested();
}

void AskPassword::password_dialog()
{
    bool ok;
    QString title = QObject::tr("Enter private key passphrase");
    QString text = QObject::tr(QString("Passphrase for (%1)").arg(account_id).toUtf8().constData());
    QString psw = QInputDialog::getText((QWidget*)mainwindow,title,text,QLineEdit::Password,QString(),&ok);
    if (ok)
        idpasswd[account_id] = psw;
}

