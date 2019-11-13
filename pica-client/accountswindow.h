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
#ifndef ACCOUNTSWINDOW_H
#define ACCOUNTSWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include "accounts.h"
#include <QList>

class AccountsWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit AccountsWindow(QWidget *parent = 0);

private:
	QComboBox *cb_accounts;
	QPushButton *bt_login;
	QPushButton *bt_registernew;
	QPushButton *bt_addimport;
	QPushButton *bt_delete;

	Accounts accounts;
	QList<Accounts::AccountRecord> L;

	void LoadAccounts();
	void CreateAccount(QString CertFilename, QString PkeyFilename, bool copyfiles);

	void closeEvent(QCloseEvent *);


signals:

public slots:

private slots:
	void login_click();
	void register_click();
	void add_click();
	void delete_click();

};

#endif // ACCOUNTSWINDOW_H
