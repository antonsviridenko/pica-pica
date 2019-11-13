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
#ifndef DHPARAM_H
#define DHPARAM_H

#include <QObject>
#include <QString>
#include "globals.h"
#include "openssltool.h"

class DHParam : public QObject
{
	Q_OBJECT
public:
	explicit DHParam(QObject *parent = 0);
	static bool VerifyDefault(QString *errormsg);
	static bool VerifyGenerated(QString *errormsg);
	static bool UseDefault();
	static bool StartNewDHParamGeneration(QString *errormsg);

	static QString GetDHParamFilename();

signals:

public slots:

private:
	static DHParam *dhparamgenerator;
	static QString current_dhparam_file;

	OpenSSLTool osslt;

private slots:
	void DHParamGenFinished(int retval, QProcess::ExitStatus);
};

#endif // DHPARAM_H
