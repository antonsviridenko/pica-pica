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
#ifndef NODESDIALOG_H
#define NODESDIALOG_H

#include <QDialog>

class QListWidget;
class QPushButton;

class NodesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit NodesDialog(QWidget *parent = 0);

signals:

public slots:
private slots:
	void OK();
	void Delete();
	void Add();

private:
	QListWidget *lwNodes;
	QPushButton *pbOk;
	QPushButton *pbDelete;
	QPushButton *pbAdd;
};

#endif // NODESDIALOG_H
