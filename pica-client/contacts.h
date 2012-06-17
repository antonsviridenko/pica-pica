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
        quint32 id;
        QString name;
    };

    Contacts(QString storage, quint32 user_account_id);

    void Add(quint32 id);
    void Delete(quint32 id);
    QList<ContactRecord> GetContacts();
    QString GetContactCert(quint32 id);
    void SetContactCert(quint32 id, QString &cert_pem);
    void SetContactName(quint32 id, QString name);
    QString GetLastError();
    bool isOK();
private:
    QSqlDatabase dbconn;
    QSqlError lasterr;
    quint32 account_id_;
};

#endif // CONTACTS_H
