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
#ifndef PICASYSTRAY_H
#define PICASYSTRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QPixmap>
#include "skynet.h"
#include "mainwindow.h"
#include "globals.h"

class PicaSysTray : public QObject
{
	Q_OBJECT
public:
	explicit PicaSysTray(QObject *parent = 0);
	void StartBlinking();
	void StopBlinking();

signals:
	void doubleclicked();

public slots:

private:
	QSystemTrayIcon *systray_;
	QMenu *systrayMenu_;
	void timerEvent(QTimerEvent *event);
	int timer_id_;
	QIcon blink_;
	QPixmap emptyicon_;

private slots:
	void systray_activated(QSystemTrayIcon::ActivationReason);
	void skynet_became_self_aware();//debug
	void skynet_lost_self_awareness();//debug
};

#endif // PICASYSTRAY_H
