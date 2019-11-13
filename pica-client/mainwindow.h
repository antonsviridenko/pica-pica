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
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QComboBox>
#include <QSystemTrayIcon>
#include <QPushButton>

#include "contacts.h"
#include "contactlistwidget.h"
#include "askpassword.h"

namespace Ui
{
class MainWindow;
}

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = 0);
	~MainWindow();

	void AddNotification(QString &text, bool is_critical);
protected:

private:
	void createMenus();

	Ui::MainWindow *ui;
	ContactListWidget *contact_list;
	QComboBox *status;
	QListWidget *notifications;
	QPushButton *bt_showhide_notifications;

	QMenu *contactsMenu;
	QMenu *helpMenu;
	QMenu *nodesMenu;
	QMenu *accountMenu;
	QMenu *settingsMenu;

	bool status_change_disable_flag;
	void SetStatus(bool connected);
	bool status_notifications_hidden;

private slots:
	void set_online();
	void set_offline();
	void status_changed(int index);
	void show_notifications();
	void hide_notifications();
	void showhide_click();

};

#endif // MAINWINDOW_H
