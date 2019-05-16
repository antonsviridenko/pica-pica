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
	void SetContactType(QByteArray id, ContactType type);
	QString GetLastError();
	bool isOK();

private:
	QSqlDatabase dbconn;
	QSqlError lasterr;
	QByteArray account_id_;
};

#endif // CONTACTS_H
