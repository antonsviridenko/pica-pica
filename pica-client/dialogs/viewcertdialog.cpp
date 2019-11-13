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
#include "viewcertdialog.h"
#include "../openssltool.h"
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFile>

ViewCertDialog::ViewCertDialog(QWidget *parent) :
	QDialog(parent)
{
	QVBoxLayout *layout;

	layout = new QVBoxLayout;
	cert_text = new QTextEdit(this);
	btOK = new QPushButton(tr("OK"));
	btOK->setMaximumWidth(45);
	this->setMinimumWidth(640);
	this->setMinimumHeight(480);

	QTextCharFormat fmt;

	fmt = cert_text->currentCharFormat();
	fmt.setFontFixedPitch(true);
	fmt.setFontStyleHint(QFont::Monospace);
	fmt.setFontFamily("Liberation Mono");
	cert_text->setCurrentCharFormat(fmt);

	cert_text->setReadOnly(true);

	layout->addWidget(cert_text);
	layout->addWidget(btOK, 0, Qt::AlignCenter);

	setLayout(layout);

	connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));

}

void ViewCertDialog::SetCert(QString cert_pem)
{
	cert_text->setPlainText(OpenSSLTool::CertTextFromString(cert_pem));
}

void ViewCertDialog::OK()
{
	done(0);
}
