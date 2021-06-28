/*
	(c) Copyright  2012 - 2021 Anton Sviridenko
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
#ifndef AUDIOVIDEOCALLCONTROLLER_H
#define AUDIOVIDEOCALLCONTROLLER_H

#include <QObject>
#include "callwindow.h"

class AudioVideoCallController : public QObject
{
	Q_OBJECT
public:
	explicit AudioVideoCallController(QObject *parent = nullptr);
public slots:
	void start_call(QByteArray peer_id);
signals:
private:
	CallWindow *callwindow;
};

#endif // AUDIOVIDEOCALLCONTROLLER_H
