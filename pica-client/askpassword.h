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
    static int ask_password_cb(char *buf, int size, int rwflag, void *userdata);

private:
    void emit_sig();
    static QMap<QByteArray,QString> idpasswd;
    static QMutex passwd_mutex;

signals:
    void password_requested();

public slots:
    void password_dialog();
};

#endif // ASKPASSWORD_H
