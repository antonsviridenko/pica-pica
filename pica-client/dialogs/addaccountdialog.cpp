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
#include "addaccountdialog.h"
#include <QGridLayout>
#include <QLabel>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include "../../PICA_client.h"

AddAccountDialog::AddAccountDialog(QWidget *parent) :
	QDialog(parent)
{
	QGridLayout *grid = new QGridLayout(this);

	QLabel *lb_cert_file = new QLabel(tr("Pica Pica Certificate File:"));
	QLabel *lb_pkey_file = new QLabel(tr("Private Key File:"));

	btOK = new QPushButton(tr("OK"));
	btCancel = new QPushButton(tr("Cancel"));

	cert_filename = new QLineEdit();
	pkey_filename = new QLineEdit();

	bt_browse_cert = new QPushButton("...");
	bt_browse_pkey = new QPushButton("...");

	rb_copyfiles = new QRadioButton(tr("Copy files to config directory"));
	rb_readinplace = new QRadioButton(tr("Read files from selected place"));

	grid->addWidget(lb_cert_file, 0, 0, 1, 1);

	grid->addWidget(cert_filename, 1, 0, 1, 1);
	grid->addWidget(bt_browse_cert, 1, 1, 1, 1);

	grid->addWidget(lb_pkey_file, 2, 0, 1, 1);

	grid->addWidget(pkey_filename, 3, 0, 1, 1);
	grid->addWidget(bt_browse_pkey, 3, 1, 1, 1);

	grid->addWidget(rb_copyfiles, 4, 0, 1, 1);
	grid->addWidget(rb_readinplace, 5, 0, 1, 1);

	grid->addWidget(btOK, 6, 0, 1, 1, Qt::AlignRight);
	grid->addWidget(btCancel, 6, 1, 1, 1, Qt::AlignLeft);
	//===========
	rb_copyfiles->setChecked(true);

	btOK->setMaximumWidth(90);
	btCancel->setMaximumWidth(90);

	connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));
	connect(btCancel, SIGNAL(clicked()), this, SLOT(Cancel()));
	connect(bt_browse_cert, SIGNAL(clicked()), this, SLOT(browse_cert()));
	connect(bt_browse_pkey, SIGNAL(clicked()), this, SLOT(browse_pkey()));

	setLayout(grid);
}

void AddAccountDialog::OK()
{
	{
		QString s;
		if (!QFile::exists(s = cert_filename->text()) || !QFile::exists(s = pkey_filename->text()) )
		{
			QMessageBox mbx;
			mbx.setText(tr("File %1 does not exist").arg(s));
			mbx.exec();
			return;
		}

		QByteArray id(PICA_ID_SIZE, 0);

		if (!PICA_get_id_from_cert_file(cert_filename->text().toUtf8().constData(), (unsigned char*)id.data()))
		{
			QMessageBox mbx;
			mbx.setText(tr("Invalid certificate file"));
			mbx.exec();
			return;
		}
	}

	done(1);

}

void AddAccountDialog::Cancel()
{
	done(0);
}

void AddAccountDialog::browse_cert()
{
	cert_filename->setText(QFileDialog::getOpenFileName(this, tr("Select Pica Pica Certificate"), "", "PEM file (*.pem)"));
}

void AddAccountDialog::browse_pkey()
{
	pkey_filename->setText(QFileDialog::getOpenFileName(this, tr("Select Private Key"), "", "PEM file (*.pem)"));
}

QString AddAccountDialog::GetCertFilename()
{
	return cert_filename->text();
}

QString AddAccountDialog::GetPkeyFilename()
{
	return pkey_filename->text();
}

bool AddAccountDialog::isCopyFilesChecked()
{
	return rb_copyfiles->isChecked();
}
