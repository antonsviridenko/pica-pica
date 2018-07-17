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
#include "dhparam.h"
#include <QFile>
#include <QDir>


DHParam *DHParam::dhparamgenerator = NULL;
QString DHParam::current_dhparam_file;

DHParam::DHParam(QObject *parent) :
	QObject(parent)
{
}


bool DHParam::VerifyDefault(QString *errormsg)
{
	if (!QFile::exists(config_defaultDHParam))
	{
		if (errormsg)
			*errormsg =
			    QString(tr("Diffie-Hellman parameters file \"%1\" does not exist"))
			    .arg(config_defaultDHParam);

		return false;
	}

	return true;
}

bool DHParam::VerifyGenerated(QString *errormsg)
{
	if (!QFile::exists(config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE))
		return false;

	return true;
}

bool DHParam::UseDefault()
{
	return QFile::copy(config_defaultDHParam, config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE);
}

bool DHParam::StartNewDHParamGeneration(QString *errormsg)
{
	if (dhparamgenerator)
	{
		if (errormsg)
			*errormsg = QString(tr("Diffie-Hellman parameter generation process is already running"));
		return false;
	}

	dhparamgenerator = new DHParam();

	if (!dhparamgenerator)
		return false;

	QString tempdhfile = config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE + ".temp";

	if (QFile::exists(tempdhfile))
		QFile::remove(tempdhfile);

	return dhparamgenerator -> osslt.GenDHParamSignal(PICA_CLIENT_DHPARAMBITS,
	        tempdhfile, dhparamgenerator, SLOT(DHParamGenFinished(int, QProcess::ExitStatus)));
}

QString DHParam::GetDHParamFilename()
{
	if (current_dhparam_file.isEmpty())
	{
		if (QFile::exists(config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE))
			current_dhparam_file = config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE;
		else
			current_dhparam_file = config_defaultDHParam;
	}

	return current_dhparam_file;
}

void DHParam::DHParamGenFinished(int retval, QProcess::ExitStatus)
{
	if (retval == 0)
	{
		if (QFile::exists(config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE))
			QFile::remove(config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE);

		QFile::rename(config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE + ".temp",
		              config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE);

		current_dhparam_file = config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE;

		delete dhparamgenerator;

		dhparamgenerator = NULL;
	}
}
