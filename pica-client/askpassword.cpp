#include "askpassword.h"
#include "globals.h"
#include <string.h>
#include <QInputDialog>
#include <QThread>
#include "accounts.h"

QMutex AskPassword::passwd_mutex;
char AskPassword::current_password[256];
int AskPassword::is_invalid = 0;
int AskPassword::is_cancelled = 0;
int AskPassword::is_set = 0;

AskPassword::AskPassword(QObject *parent) :
	QObject(parent)
{
	connect(this, SIGNAL(password_requested()), this, SLOT(password_dialog()), Qt::BlockingQueuedConnection);
}

AskPassword::~AskPassword()
{
	memset(current_password, 0, sizeof(current_password));
}

int AskPassword::ask_password_cb(char *buf, int size, int rwflag, void *userdata)
{
	//QString pswd;
	//QByteArray id = QByteArray((const char*) userdata, PICA_ID_SIZE);

	if (is_cancelled)
		return 0;

	passwd_mutex.lock();

	if (!is_set || is_invalid)
	{
		if (askpassword->thread() != QThread::currentThread())
			askpassword->emit_sig();
		else
			askpassword->password_dialog();
	}

	if (is_set)
	{
		strncpy(buf, current_password, size);
		buf[size - 1] = 0;
	}

	passwd_mutex.unlock();


	return is_set ? (strlen(buf)) : 0;
}

void AskPassword::emit_sig()
{
	emit password_requested();
}

void AskPassword::clear()
{
	passwd_mutex.lock();
	is_invalid = 0;
	is_set  = 0;
	is_cancelled = 0;
	memset(current_password, 0, sizeof(current_password));
	passwd_mutex.unlock();

}

void AskPassword::setInvalidPassword()
{
	passwd_mutex.lock();
	is_invalid = 1;
	memset(current_password, 0, sizeof(current_password));
	passwd_mutex.unlock();
}

void AskPassword::password_dialog()
{
	bool ok = false;
	QString title = is_invalid ? QObject::tr("Invalid passphrase, try again") : QObject::tr("Enter private key passphrase");
	QString text = QObject::tr(QString("Passphrase for account %1 \n (%2)")
	                           .arg(Accounts::GetCurrentAccount().name)
	                           .arg(Accounts::GetCurrentAccount().id.toBase64().constData()).toUtf8().constData());
	QString psw = QInputDialog::getText((QWidget*)mainwindow, title, text, QLineEdit::Password, QString(), &ok);

	if (ok)
	{
		strncpy(current_password, psw.toUtf8().constData(), sizeof(current_password));
		current_password[sizeof(current_password) - 1] = 0;
		is_set = 1;
		is_invalid = 0;
	}
	else
		is_cancelled = 1;
}

