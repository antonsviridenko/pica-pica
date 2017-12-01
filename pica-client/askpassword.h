#ifndef ASKPASSWORD_H
#define ASKPASSWORD_H

#include <QObject>
#include <QMap>
#include <QMutex>

class AskPassword : public QObject
{
	Q_OBJECT
public:
	explicit AskPassword(QObject *parent = 0);
	~AskPassword();
	static int ask_password_cb(char *buf, int size, int rwflag, void *userdata);
	static void setInvalidPassword();
	static void clear();

private:
	void emit_sig();
	static QMutex passwd_mutex;
	static char current_password[256];
	static int is_cancelled;
	static int is_invalid;
	static int is_set;

signals:
	void password_requested();

public slots:
	void password_dialog();
};

#endif // ASKPASSWORD_H
