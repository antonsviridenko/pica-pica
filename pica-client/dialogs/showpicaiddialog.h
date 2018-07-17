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
#ifndef SHOWPICAIDDIALOG_H
#define SHOWPICAIDDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include <QByteArray>

class ShowPicaIdDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ShowPicaIdDialog(QString name, QByteArray id, QString caption, QWidget *parent = 0);

private slots:
	void Ok();

private:
	QLabel *lbname_;
	QLineEdit *picaid_;
	QPushButton *btok_;

};

#endif // SHOWPICAIDDIALOG_H
