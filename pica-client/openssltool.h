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
#ifndef OPENSSLTOOL_H
#define OPENSSLTOOL_H

#include <QString>
#include <QProcess>

class OpenSSLTool
{
public:
	OpenSSLTool();

	static QString NameFromCertFile(QString cert_file);
	static QString NameFromCertString(QString cert_pem);
	static QString CertTextFromString(QString cert_pem);

	bool GenRSAKeySignal(QString keyfile, bool setpassword,
	                     QString password, QString rand, QObject *receiver, const char *finished_slot);
	bool GenCertSignal(QString cert_file, QString keyfile, QString keypassword, QString subject, QObject *receiver, const char *finished_slot);

	bool GenDHParamSignal(quint32 numbits, QString output_file, QObject *receiver, const char *finished_slot);

	QString ReadStdErr();
	QString ReadStdOut();

private:
	QProcess openssl_;
};

#endif // OPENSSLTOOL_H
