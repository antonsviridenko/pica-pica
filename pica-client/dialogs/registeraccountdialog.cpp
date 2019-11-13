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
#include "registeraccountdialog.h"
#include "../globals.h"
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QMessageBox>


RegisterAccountDialog::RegisterAccountDialog(QWidget *parent) :
	QDialog(parent)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	QLabel *lbNickname = new QLabel(tr("Name:"));
	lbPassword = new QLabel(tr("Password:"));
	lbRepeatPassword = new QLabel(tr("Repeat password:"));

	nickname = new QLineEdit();
	btRegister = new QPushButton(tr("Create"));

#ifndef WIN32
	cbUseDevRandom = new QCheckBox(tr("Use /dev/random as a random data source"));
#endif

	cbSetPassword = new QCheckBox(tr("Set passphrase for the secret key"));
	password = new QLineEdit();
	repeatPassword = new QLineEdit();
	lbStatus = new QLabel(tr("Enter your name and press\n \"Create\" to create new Pica Pica certificate"));
	//btOK = new QPushButton(tr("OK"));

	password->setEchoMode(QLineEdit::Password);
	repeatPassword->setEchoMode(QLineEdit::Password);

	{
		QFont f;
		f = lbStatus->font();
		f.setBold(true);
		lbStatus->setFont(f);
	}

	layout->addWidget(lbNickname);
	layout->addWidget(nickname);
#ifndef WIN32
	layout->addWidget(cbUseDevRandom);
#endif
	layout->addWidget(cbSetPassword);
	layout->addWidget(lbPassword);
	layout->addWidget(password);
	layout->addWidget(lbRepeatPassword);
	layout->addWidget(repeatPassword);
	layout->addWidget(btRegister);
	layout->addWidget(lbStatus);
	//layout->addWidget(btOK);

	setLayout(layout);

	vld = new QRegExpValidator(QRegExp("[^\\#\\$&\"\'=\\(\\)\\\\/\\|`!<>\\{\\}\\[\\]\\+]+"), this);
	nickname->setValidator(vld);

	//connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));
	connect(btRegister, SIGNAL(clicked()), this, SLOT(Register()));
	connect(cbSetPassword, SIGNAL(clicked()), this, SLOT(setPasswordClick()));
#ifndef WIN32
	connect(cbUseDevRandom, SIGNAL(clicked()), this, SLOT(useDevRandomClick()));
#endif

	setPasswordClick();
}

void RegisterAccountDialog::useDevRandomClick()
{
#ifndef WIN32
	if (cbUseDevRandom->isChecked())
	{
		QMessageBox mbx;
		mbx.setText(tr("Using /dev/random is more preferable for generating long-term high-value cryptographic keys, but it can take 5 - 10 minutes to collect required amount of entropy. You'll have to press random keys on keyboard and move mouse randomly to provide random data. "));
		mbx.exec();
	}
#endif
}

void RegisterAccountDialog::setPasswordClick()
{
	if (cbSetPassword->isChecked())
	{
		password -> setDisabled(false);
		repeatPassword -> setDisabled(false);

		password->show();
		repeatPassword->show();
		lbPassword->show();
		lbRepeatPassword->show();
	}
	else
	{
		password -> setDisabled(true);
		repeatPassword -> setDisabled(true);

		password->setHidden(true);
		repeatPassword->setHidden(true);
		lbPassword->hide();
		lbRepeatPassword->hide();
	}

	adjustSize();
}

//void RegisterAccountDialog::OK()
//{

//    done(0);
//}

void RegisterAccountDialog::Register()
{
	//check name
	QString name = nickname->text();
	QString rand = QString::null;

	if (name.toUtf8().size() > 63) //64 bytes - CN length in X509 certificate
	{
		QMessageBox mbx;
		mbx.setText(tr("Name is too long"));
		mbx.exec();
		return;
	}

	if (name.isEmpty())
	{
		QMessageBox mbx;
		mbx.setText(tr("You must enter account name"));
		mbx.exec();
		return;
	}

	if (cbSetPassword->isChecked())
	{
		if (password->text() != repeatPassword->text())
		{
			QMessageBox mbx;
			mbx.setText(tr("Passphrases do not match"));
			mbx.exec();
			return;
		}
	}

#ifndef WIN32
	if (cbUseDevRandom->isChecked())
	{
		rand = "/dev/random";
	}

	cbUseDevRandom -> setDisabled(true);
#endif

	cbSetPassword -> setDisabled(true);

	//1
	lbStatus->setText(tr("1) generating RSA keypair..."));

	nickname->setEnabled(false);
	btRegister->setEnabled(false);

	ost.GenRSAKeySignal(config_dir + QDir::separator() + "privkey.pem", cbSetPassword->isChecked(),
	                    password->text(), rand, this, SLOT(stageSignCert(int, QProcess::ExitStatus)));
}

void RegisterAccountDialog::stageSignCert(int retval, QProcess::ExitStatus)
{
	if (retval != 0)
	{
		lbStatus->setText(tr("Error:\n") + ost.ReadStdErr());
		return;
	}

	QFile::setPermissions(config_dir + QDir::separator() + "privkey.pem", QFile::ReadOwner | QFile::WriteOwner);

	lbStatus->setText(tr("2) Signing certificate..."));

	ost.GenCertSignal(config_dir + QDir::separator() + "cert.pem",
	                  config_dir + QDir::separator() + "privkey.pem",
	                  password->text(), nickname->text(),
	                  this, SLOT(stageFinished(int, QProcess::ExitStatus)));
}

void RegisterAccountDialog::stageFinished(int retval, QProcess::ExitStatus)
{
	if (retval != 0)
	{
		lbStatus->setText(tr("Error:\n") + ost.ReadStdErr());
		return;
	}

	CertFilename_ = config_dir + QDir::separator() + "cert.pem";
	PkeyFilename_ = config_dir + QDir::separator() + "privkey.pem";

	done(1);
}

QString RegisterAccountDialog::GetCertFilename()
{
	return CertFilename_;
}

QString RegisterAccountDialog::GetPkeyFilename()
{
	return PkeyFilename_;
}

