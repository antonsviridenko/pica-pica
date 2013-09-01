#include "askpassword.h"
#include "globals.h"
#include <string.h>
#include <QInputDialog>
#include "accounts.h"

QMutex AskPassword::passwd_mutex;
QMap<QByteArray,QString> AskPassword::idpasswd;

AskPassword::AskPassword(QObject *parent) :
    QObject(parent)
{
    connect(this,SIGNAL(password_requested()),this,SLOT(password_dialog()),Qt::BlockingQueuedConnection);
}

int AskPassword::ask_password_cb(char *buf, int size, int rwflag, void *userdata)
{
    QString pswd;
    QByteArray id = QByteArray((const char*) userdata, PICA_ID_SIZE);

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
    QString text = QObject::tr(QString("Passphrase for account %1 \n (%2)")
        .arg(Accounts::GetCurrentAccount().name)
        .arg(Accounts::GetCurrentAccount().id.toBase64().constData()).toUtf8().constData());
    QString psw = QInputDialog::getText((QWidget*)mainwindow,title,text,QLineEdit::Password,QString(),&ok);
    if (ok)
        idpasswd[Accounts::GetCurrentAccount().id] = psw;
}

