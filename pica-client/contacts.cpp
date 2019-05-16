/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "contacts.h"


//#include <QMessageBox>

Contacts::Contacts(QString storage, QByteArray user_account_id)
	: account_id_(user_account_id)
{
	dbconn = QSqlDatabase::database();
	dbconn.setDatabaseName(storage);

	if (!dbconn.open())
	{
		lasterr = dbconn.lastError();
		return;
	}

//check if table contacts exists
//...

	QSqlQuery query;

	query.exec("PRAGMA foreign_keys=ON;");

	//check result, if foreign keys support is on
}

bool Contacts::isOK()
{
	return  !lasterr.isValid();
}

QString Contacts::GetLastError()
{
	return lasterr.text();
}

bool Contacts::Exists(QByteArray id)
{
	QSqlQuery query;

	query.setForwardOnly(true);
	query.prepare("select count(*) from contacts where account_id=:account_id and id=:id");
	query.bindValue(":account_id", account_id_);
	query.bindValue(":id", id);
	query.exec();
	lasterr = query.lastError();

	query.next();
	if (query.value(0).toInt() == 1)
		return true;

	return false;
}

void Contacts::Add(QByteArray id, ContactType type)
{
	QSqlQuery query;

	if (!Exists(id))
	{
		query.prepare("insert into contacts (id, name, cert_pem, account_id, type) values (:id, NULL, NULL, :account_id, :type)");
		query.bindValue(":id", id);
		query.bindValue(":account_id", account_id_);
		query.bindValue(":type", type);
		query.exec();
	}
	else
	{
		query.prepare("update contacts set type=:type where id=:id and account_id=:account_id");
		query.bindValue(":type", type);
		query.bindValue(":id", id);
		query.bindValue(":account_id", account_id_);
		query.exec();
	}

	lasterr = query.lastError();
}

void Contacts::Delete(QByteArray id)
{
	QSqlQuery query;

	query.prepare("delete from contacts where id=:id and account_id=:account_id");
	query.bindValue(":id", id);
	query.bindValue(":account_id", account_id_);
	query.exec();

	lasterr = query.lastError();
}

QList<Contacts::ContactRecord> Contacts::GetContacts(ContactType type)
{
	QList<ContactRecord> L;
	ContactRecord r;
	QSqlQuery query;

	query.setForwardOnly(true);
	query.prepare("select id, name from contacts where account_id=:account_id and type=:type");
	query.bindValue(":account_id", account_id_);
	query.bindValue(":type", type);
	query.exec();
	lasterr = query.lastError();

	if (lasterr.isValid())
		return L;

	while(query.next())
	{
		r.id = query.value(0).toByteArray();
		r.name = query.value(1).toString();
		r.type = type;
		//CSTRL.append(" ("+query.value(0).toString()+") "+query.value(1).toString());
		L << r;
	}

	return L;
}

QString Contacts::GetContactCert(QByteArray id)
{
	QSqlQuery query;

	query.prepare("select cert_pem from contacts where id=:id and account_id=:account_id");
	query.bindValue(":id", id);
	query.bindValue(":account_id", account_id_);
	query.exec();

	lasterr = query.lastError();

	if (lasterr.isValid())
		return QString::null;

	query.next();

	return query.value(0).toString();
}

QString Contacts::GetContactName(QByteArray id)
{
	QSqlQuery query;

	query.prepare("select name from contacts where id=:id and account_id=:account_id");
	query.bindValue(":id", id);
	query.bindValue(":account_id", account_id_);
	query.exec();

	lasterr = query.lastError();

	if (lasterr.isValid())
		return QString::null;

	query.next();

	return query.value(0).toString();
}

void Contacts::SetContactCert(QByteArray id, QString &cert_pem)
{
	QSqlQuery query;

	query.prepare("update contacts set cert_pem=:cert_pem where id=:id and account_id=:account_id");
	query.bindValue(":cert_pem", cert_pem);
	query.bindValue(":id", id);
	query.bindValue(":account_id", account_id_);
	query.exec();

	lasterr = query.lastError();

}

void Contacts::SetContactName(QByteArray id, QString name)
{
	QSqlQuery query;

	query.prepare("update contacts set name=:name where id=:id and account_id=:account_id");
	query.bindValue(":name", name);
	query.bindValue(":id", id);
	query.bindValue(":account_id", account_id_);
	query.exec();

	lasterr = query.lastError();
}

void Contacts::SetContactType(QByteArray id, ContactType type)
{
	QSqlQuery query;

	query.prepare("update contacts set type=:type where id=:id and account_id=:account_id");
	query.bindValue(":type", type);
	query.bindValue(":id", id);
	query.bindValue(":account_id", account_id_);
	query.exec();

	lasterr = query.lastError();
}

