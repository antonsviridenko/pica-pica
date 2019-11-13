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
#ifndef ADDACCOUNTDIALOG_H
#define ADDACCOUNTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QRadioButton>

class AddAccountDialog : public QDialog
{
	Q_OBJECT
public:
	explicit AddAccountDialog(QWidget *parent = 0);
	QString GetCertFilename();
	QString GetPkeyFilename();
	bool isCopyFilesChecked();

private:
	QPushButton *btOK;
	QPushButton *btCancel;

	QLineEdit *cert_filename;
	QLineEdit *pkey_filename;

	QPushButton *bt_browse_cert;
	QPushButton *bt_browse_pkey;

	QRadioButton *rb_copyfiles;
	QRadioButton *rb_readinplace;
signals:

public slots:
private slots:
	void OK();
	void Cancel();
	void browse_cert();
	void browse_pkey();

};

#endif // ADDACCOUNTDIALOG_H
