#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <QString>
#include <QtSql>
#include <QSqlDatabase>
#include <QStringList>

class Accounts
{
public:
    struct AccountRecord
    {
        QByteArray id;
        QString name;
        QString cert_file;
        QString pkey_file;
        QString CA_file;
    };

    Accounts(QString storage);
    void Add(AccountRecord& acc);
    void Delete(QByteArray id);
    QList<AccountRecord> GetAccounts();
    QString GetName(QByteArray id);
    bool isOK();
    QString GetLastError();

    static bool CheckFiles(AccountRecord& acc, QString &error_string);

private:
    QSqlDatabase dbconn;
    QSqlError lasterr;
};

#endif // ACCOUNTS_H
