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
#ifndef REGISTERACCOUNTDIALOG_H
#define REGISTERACCOUNTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QRegExpValidator>
#include "../openssltool.h"

class RegisterAccountDialog : public QDialog
{
	Q_OBJECT
public:
	explicit RegisterAccountDialog(QWidget *parent = 0);
	QString GetCertFilename();
	QString GetPkeyFilename();

private:
	//QPushButton *btOK;
	QPushButton *btRegister;
	QLineEdit *nickname;
	QLabel *lbStatus;
	QCheckBox *cbSetPassword;
	QLineEdit *password;
	QLineEdit *repeatPassword;
	QLabel *lbPassword ;
	QLabel *lbRepeatPassword;
	QCheckBox *cbUseDevRandom;

	QRegExpValidator *vld;
	OpenSSLTool ost;
	QByteArray cert_buf;

	QString CertFilename_;
	QString PkeyFilename_;
signals:

private slots:
	//void OK();
	void Register();
	void setPasswordClick();
	void useDevRandomClick();

	void stageSignCert(int retval, QProcess::ExitStatus);
	void stageFinished(int retval, QProcess::ExitStatus);
};

#endif // REGISTERACCOUNTDIALOG_H
