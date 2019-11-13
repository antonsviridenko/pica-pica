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
#ifndef FORGEDCERTDIALOG_H
#define FORGEDCERTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include "viewcertdialog.h"

class ForgedCertDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ForgedCertDialog(QByteArray peer_id, QString received_cert, QString stored_cert, QWidget *parent = 0);

private:
	QString received_cert_;
	QString stored_cert_;

	QPushButton *btOK;
	QPushButton *btShow_stored_cert;
	QPushButton *btShow_received_cert;


signals:

public slots:

private slots:
	void OK();
	void StoredCert();
	void ReceivedCert();

};

#endif // FORGEDCERTDIALOG_H
