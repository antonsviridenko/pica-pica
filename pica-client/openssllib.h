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
#ifndef OPENSSLLIB_H
#define OPENSSLLIB_H

#include <QObject>
#include <QString>

/*
 * Key, certificate and Diffie-Hellman parameter generation, and the reading
 * of certificates for display, done through libcrypto.
 *
 * This is the replacement for OpenSSLTool, which drove the "openssl" command
 * line program through QProcess and parsed its output. The results are meant
 * to be the same down to the encoding of the files produced - see
 * tests/test_openssllib.cpp, which compares them against the command line
 * program - but the binary is no longer a runtime dependency, there is no
 * openssl.cnf to ship next to the executable, and passphrases no longer
 * travel through a pipe.
 *
 * The three generating methods keep the shape of their OpenSSLTool
 * counterparts: they return immediately and call the receiver's slot when the
 * work is over. The work itself happens on a worker thread, because it is the
 * GUI thread calling - RSA key generation takes seconds and DH parameter
 * generation at PICA_CLIENT_DHPARAMBITS takes minutes. The slot signature is
 * the one difference from OpenSSLTool: there is no process any more, so it is
 * finished(int retval) rather than finished(int, QProcess::ExitStatus).
 * retval keeps its old meaning - zero on success.
 */

class OpenSSLLibWorker;

class OpenSSLLib : public QObject
{
	Q_OBJECT
public:
	explicit OpenSSLLib(QObject *parent = 0);
	~OpenSSLLib();

	static QString NameFromCertFile(QString cert_file);
	static QString NameFromCertString(QString cert_pem);
	static QString CertTextFromString(QString cert_pem);

	bool GenRSAKeySignal(QString keyfile, bool setpassword,
	                     QString password, QString rand, QObject *receiver, const char *finished_slot);
	bool GenCertSignal(QString cert_file, QString keyfile, QString keypassword, QString subject, QObject *receiver, const char *finished_slot);

	bool GenDHParamSignal(quint32 numbits, QString output_file, QObject *receiver, const char *finished_slot);

	QString ReadStdErr();
	QString ReadStdOut();

signals:
	void finished(int retval);

private slots:
	void WorkerFinished();

private:
	bool StartWorker(OpenSSLLibWorker *worker, QObject *receiver, const char *finished_slot);

	OpenSSLLibWorker *worker_;
	QString errors_;
	QString output_;
};

#endif // OPENSSLLIB_H
