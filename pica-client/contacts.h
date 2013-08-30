#ifndef CONTACTS_H
#define CONTACTS_H

#include <QString>
#include <QtSql>
#include <QSqlDatabase>
#include <QStringList>

class Contacts
{
public:
    struct ContactRecord
    {
        QByteArray id;
        QString name;
    };

    Contacts(QString storage, QByteArray user_account_id);

    void Add(QByteArray id);
    void Delete(QByteArray id);
    QList<ContactRecord> GetContacts();
    QString GetContactCert(QByteArray id);
    QString GetContactName(QByteArray id);
    void SetContactCert(QByteArray id, QString &cert_pem);
    void SetContactName(QByteArray id, QString name);
    QString GetLastError();
    bool isOK();
private:
    QSqlDatabase dbconn;
    QSqlError lasterr;
    QByteArray account_id_;
};

#endif // CONTACTS_H
