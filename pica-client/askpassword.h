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
#ifndef ASKPASSWORD_H
#define ASKPASSWORD_H

#include <QObject>
#include <QMap>
#include <QMutex>

class AskPassword : public QObject
{
	Q_OBJECT
public:
	explicit AskPassword(QObject *parent = 0);
	~AskPassword();
	static int ask_password_cb(char *buf, int size, int rwflag, void *userdata);
	static void setInvalidPassword();
	static void clear();

private:
	void emit_sig();
	static QMutex passwd_mutex;
	static char current_password[256];
	static int is_cancelled;
	static int is_invalid;
	static int is_set;

signals:
	void password_requested();

public slots:
	void password_dialog();
};

#endif // ASKPASSWORD_H
