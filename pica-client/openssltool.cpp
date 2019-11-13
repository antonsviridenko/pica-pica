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
#include "openssltool.h"
#include "../PICA_security.h"
#include <openssl/rand.h>
#include <QProcess>
#include <QRegExp>
#include <QMessageBox>

#define REGEXOLD "CN = [0-9]+\\#([^\\#\\$&\"\'=\\(\\)\\\\/\\|`!<>\\{\\}\\[\\]\\+\r\n]+)"
#define REGEXNEW "CN = ([^\\#\\$&\"\'=\\(\\)\\\\/\\|`!<>\\{\\}\\[\\]\\+\r\n]+)"

OpenSSLTool::OpenSSLTool()
{
}

QString OpenSSLTool::NameFromCertFile(QString cert_file)
{
	//
	QProcess openssl;

	openssl.start("openssl", QStringList() << "x509" << "-in" << cert_file << "-subject" << "-nameopt" << "oneline,-esc_msb,utf8" << "-noout");
	if (!openssl.waitForStarted())
		return QString();

	if (!openssl.waitForFinished())
		return QString();

	QByteArray result = openssl.readAll();
	QRegExp rxOld(REGEXOLD);
	QRegExp rx(REGEXNEW);

	QString name = QString::fromUtf8( result.constData());

	if (name.contains(rxOld))
	{
		name = rxOld.cap(1);
	}
	else if (name.contains(rx))
	{
		name = rx.cap(1);
	}

	//
	return name;
}

QString OpenSSLTool::NameFromCertString(QString cert_pem)
{
	QProcess openssl;

	openssl.start("openssl", QStringList() << "x509" << "-subject" << "-nameopt" << "oneline,-esc_msb,utf8" << "-noout");
	if (!openssl.waitForStarted())
		return QString();

	openssl.write(cert_pem.toLatin1());

	if (!openssl.waitForFinished())
		return QString();

	QByteArray result = openssl.readAll();
	QRegExp rxOld(REGEXOLD);
	QRegExp rx(REGEXNEW);

	QString name = QString::fromUtf8( result.constData());

	if (name.contains(rxOld))
	{
		name = rxOld.cap(1);
	}
	else if (name.contains(rx))
	{
		name = rx.cap(1);
	}

	//
	return name;
}

QString OpenSSLTool::CertTextFromString(QString cert_pem)
{
	QProcess openssl;
	qint64 ret;
	char *data;
	QByteArray ba;

	openssl.start("openssl", QStringList() << "x509" << "-noout" << "-text" << "-nameopt" << "multiline,-esc_msb,utf8");

	if (!openssl.waitForStarted())
		return QString();

	ba = cert_pem.toLatin1();
	ret = openssl.write(data = (char*)ba.constData());
	openssl.closeWriteChannel();

	if (!openssl.waitForFinished())
		return QString();

	return QString::fromUtf8(openssl.readAllStandardOutput().constData());
}

bool OpenSSLTool::GenRSAKeySignal(QString keyfile, bool setpassword, QString password, QString rand, QObject *receiver, const char *finished_slot)
{
	QStringList args;
	quint32 numbits;
	unsigned char rndchar;

	args << "genrsa" << "-out" << keyfile;

	if (setpassword)
		args << "-passout" << "stdin" << PICA_PRIVKEYENCALGO;

	if (!rand.isEmpty())
		args << "-rand" << rand;

	/*
	 * Generate random-sized key with min size 4096 and multiply of 8.
	 * Max size is limited to 4096 + 99*8.
	 * See https://blog.josefsson.org/2016/11/03/why-i-dont-use-2048-or-4096-rsa-key-sizes/
	 * "Why I don’t Use 2048 or 4096 RSA Key Sizes"
	 */
	numbits = PICA_RSA_MINKEYSIZE;

	RAND_pseudo_bytes(&rndchar, 1);

	numbits += 8 * (rndchar % 100);

	args << QString::number(numbits);

	openssl_.start("openssl", args);


	if (!openssl_.waitForStarted())
		return false;

	if (setpassword)
	{
		openssl_.write((password + "\n").toLatin1().constData());
	}

	openssl_.disconnect(&openssl_, SIGNAL(finished(int, QProcess::ExitStatus)), 0, 0);
	openssl_.connect(&openssl_, SIGNAL(finished(int, QProcess::ExitStatus)), receiver, finished_slot);

	return true;
}

bool OpenSSLTool::GenCertSignal(QString cert_file, QString keyfile, QString keypassword,
                                QString subject, QObject *receiver, const char *finished_slot)
{
	openssl_.start("openssl",
	               QStringList()
	               << "req"
#ifdef WIN32
	               << "-config" << "openssl.cnf"
#endif
	               << "-out" << cert_file
	               << "-key" << keyfile
	               << "-subj" << "/CN=" + subject
	               << "-new"
	               << "-x509"
	               << "-days" << "3680"
	               << "-utf8"
	               << "-batch"
	               << PICA_CERTDIGESTALGO
	               << "-passin"
	               << "stdin");

	if (!openssl_.waitForStarted())
		return false;

	openssl_.write((keypassword + "\n").toLatin1().constData());

	openssl_.disconnect(&openssl_, SIGNAL(finished(int, QProcess::ExitStatus)), 0, 0);
	openssl_.connect(&openssl_, SIGNAL(finished(int, QProcess::ExitStatus)), receiver, finished_slot);

	return true;
}

QString OpenSSLTool::ReadStdErr()
{
	return QString::fromUtf8(openssl_.readAllStandardError().constData());
}

QString OpenSSLTool::ReadStdOut()
{
	return QString::fromUtf8(openssl_.readAllStandardOutput().constData());
}

bool OpenSSLTool::GenDHParamSignal(quint32 numbits, QString output_file, QObject *receiver, const char *finished_slot)
{
	//openssl dhparam -5 4096
	openssl_.start("openssl",
	               QStringList()
	               << "dhparam"
	               << "-out" << output_file
	               << "-5"
	               << QString::number(numbits));

	if (!openssl_.waitForStarted())
		return false;

	openssl_.disconnect(&openssl_, SIGNAL(finished(int, QProcess::ExitStatus)), 0, 0);
	openssl_.connect(&openssl_, SIGNAL(finished(int, QProcess::ExitStatus)), receiver, finished_slot);

	return true;
}


