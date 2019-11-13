/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
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

	static AccountRecord &GetCurrentAccount();
	static void SetCurrentAccount(AccountRecord &rec);

private:
	static AccountRecord current_account;
	QSqlDatabase dbconn;
	QSqlError lasterr;


};

#endif // ACCOUNTS_H
