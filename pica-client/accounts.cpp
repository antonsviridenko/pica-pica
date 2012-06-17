#include "accounts.h"
#include <QFile>

Accounts::Accounts(QString storage)
{
    dbconn=QSqlDatabase::addDatabase("QSQLITE");
    dbconn.setDatabaseName(storage);

    if (!dbconn.open())
    {
    lasterr=dbconn.lastError();
    return;
    }

    QSqlQuery query;

    query.exec("PRAGMA foreign_keys=ON;");

    //check result, if foreign keys support is on
}

void Accounts::Add(AccountRecord &acc)
{
    QSqlQuery query;

    query.prepare("insert into accounts (id, name, cert_file, pkey_file, ca_file) values (:id, :name, :cert_file, :pkey_file, :ca_file);");
    query.bindValue(":id",acc.id);
    query.bindValue(":name",acc.name);
    query.bindValue(":cert_file",acc.cert_file);
    query.bindValue(":pkey_file",acc.pkey_file);
    query.bindValue(":ca_file",acc.CA_file);
    query.exec();

    lasterr=query.lastError();
}

void Accounts::Delete(quint32 id)
{
    QSqlQuery query;

    query.prepare("delete from accounts where id=:id");
    query.bindValue(":id",id);
    query.exec();

    lasterr=query.lastError();
}

QList<Accounts::AccountRecord> Accounts::GetAccounts()
{
    AccountRecord r;
    QList<Accounts::AccountRecord> L;
    QSqlQuery query;

    //
    query.setForwardOnly(true);
    query.exec("select id, name, cert_file, pkey_file, ca_file from accounts");
    lasterr=query.lastError();

    if (lasterr.isValid())
        return L;

    while(query.next())
    {
        r.id = query.value(0).toUInt();
        r.name = query.value(1).toString();
        r.cert_file = query.value(2).toString();
        r.pkey_file = query.value(3).toString();
        r.CA_file = query.value(4).toString();

        L<<r;
    }

    return L;
}

bool Accounts::isOK()
{
  return  !lasterr.isValid();
}

QString Accounts::GetLastError()
{
    return lasterr.text();
}

bool Accounts::CheckFiles(AccountRecord &acc, QString &error_string)
{
  if (!QFile::exists(acc.cert_file))
  {
    error_string = QObject::tr("Certificate file %1 does not exist").arg(acc.cert_file);
    return false;
  }

  if (!QFile::exists(acc.pkey_file))
  {
    error_string = QObject::tr("Private key file %1 does not exist").arg(acc.pkey_file);
    return false;
  }

  if (!QFile::exists(acc.CA_file))
  {
    error_string = QObject::tr("Certificate Authority file %1 does not exist").arg(acc.CA_file);
    return false;
  }

  return true;
}
