#ifndef CONTACTS_H
#define CONTACTS_H

#include <QString>
#include <QtSql>
#include <QSqlDatabase>
#include <QStringList>

class Contacts
{
public:

    enum ContactType
    {
        temporary = 0,
        regular = 1,
        blacklisted = 2
    };

    struct ContactRecord
    {
        QByteArray id;
        QString name;

        ContactType type;
    };

    Contacts(QString storage, QByteArray user_account_id);

    bool Exists(QByteArray id);
    void Add(QByteArray id, ContactType type = regular);
    void Delete(QByteArray id);
    QList<ContactRecord> GetContacts(ContactType type = regular);
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
