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
#include "openssllib.h"
#include "../PICA_security.h"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/dh.h>

#include <QFile>
#include <QRegExp>
#include <QThread>

#define REGEXOLD "CN = [0-9]+\\#([^\\#\\$&\"\'=\\(\\)\\\\/\\|`!<>\\{\\}\\[\\]\\+\r\n]+)"
#define REGEXNEW "CN = ([^\\#\\$&\"\'=\\(\\)\\\\/\\|`!<>\\{\\}\\[\\]\\+\r\n]+)"

/* "-days 3680", as it was spelled on the openssl command line. */
#define PICA_CERT_VALIDITY_DAYS 3680

/*
 * The serial number width "openssl req -x509" picks when it is not given one
 * (SERIAL_RAND_BITS in apps/lib/apps.c).
 */
#define PICA_CERT_SERIAL_BITS 159

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define X509_getm_notBefore(x) X509_get_notBefore(x)
#define X509_getm_notAfter(x) X509_get_notAfter(x)
#endif

/*
 * "oneline,-esc_msb,utf8" and "multiline,-esc_msb,utf8", the two -nameopt
 * arguments OpenSSLTool used, worked out the way the command line program's
 * option parser works them out: "oneline" and "multiline" clear every other
 * bit before setting their own, the two that follow set and clear a single
 * bit each.
 */
static const unsigned long ONELINE_NAMEOPT =
    (XN_FLAG_ONELINE & ~ASN1_STRFLGS_ESC_MSB) | ASN1_STRFLGS_UTF8_CONVERT;

static const unsigned long MULTILINE_NAMEOPT =
    (XN_FLAG_MULTILINE & ~ASN1_STRFLGS_ESC_MSB) | ASN1_STRFLGS_UTF8_CONVERT;

static QString BioToString(BIO *bio)
{
	char *data = 0;
	long len;

	len = BIO_get_mem_data(bio, &data);

	if (len <= 0 || !data)
		return QString();

	return QString::fromUtf8(data, (int)len);
}

static QByteArray BioToByteArray(BIO *bio)
{
	char *data = 0;
	long len;

	len = BIO_get_mem_data(bio, &data);

	if (len <= 0 || !data)
		return QByteArray();

	return QByteArray(data, (int)len);
}

static X509 *CertFromPEM(const QByteArray &cert_pem)
{
	BIO *mem;
	X509 *cert;

	if (cert_pem.isEmpty())
		return 0;

	mem = BIO_new_mem_buf((void*)cert_pem.constData(), cert_pem.size());

	if (!mem)
		return 0;

	cert = PEM_read_bio_X509(mem, NULL, NULL, NULL);

	BIO_free(mem);
	ERR_clear_error();

	return cert;
}

/*
 * The whole file is read through QFile rather than handed to BIO_new_file():
 * config_dir can hold characters that do not survive the trip through the
 * local 8 bit encoding a C file name would have to be, and Qt already knows
 * how to open those paths on every platform we build for.
 */
static QByteArray ReadWholeFile(const QString &name)
{
	QFile f(name);

	if (!f.open(QIODevice::ReadOnly))
		return QByteArray();

	return f.readAll();
}

/*
 * The name as the "-subject -nameopt oneline,-esc_msb,utf8" output of the
 * command line program spelled it, prefix and trailing newline included, so
 * that the two regular expressions below see exactly what they used to see.
 */
static QString SubjectLine(X509 *cert)
{
	BIO *mem;
	QString line;

	mem = BIO_new(BIO_s_mem());

	if (!mem)
		return QString();

	BIO_puts(mem, "subject=");
	X509_NAME_print_ex(mem, X509_get_subject_name(cert), 0, ONELINE_NAMEOPT);
	BIO_puts(mem, "\n");

	line = BioToString(mem);

	BIO_free(mem);

	return line;
}

static QString NameFromSubjectLine(QString subject_line)
{
	QRegExp rxOld(REGEXOLD);
	QRegExp rx(REGEXNEW);

	QString name = subject_line;

	if (name.contains(rxOld))
	{
		name = rxOld.cap(1);
	}
	else if (name.contains(rx))
	{
		name = rx.cap(1);
	}

	return name;
}

/*
 * The generating half of the class. RSA key generation takes seconds and DH
 * parameter generation at PICA_CLIENT_DHPARAMBITS takes minutes, and the
 * thread calling is the one drawing the dialog that shows the progress, so
 * the work happens here instead.
 *
 * No Q_OBJECT: the only signal needed is QThread::finished(), which QThread
 * already declares.
 */
class OpenSSLLibWorker : public QThread
{
public:
	enum Job
	{
		JobGenRSAKey,
		JobGenCert,
		JobGenDHParam
	};

	OpenSSLLibWorker(Job job, QObject *parent)
		: QThread(parent), setpassword_(false), numbits_(0), job_(job), retval_(1)
	{
	}

	/* Set by the caller before start(), read by it after finished(). */
	QString keyfile_;
	QString certfile_;
	QString outfile_;
	QString password_;
	QString rand_;
	QString subject_;
	bool setpassword_;
	quint32 numbits_;

	int retval() const
	{
		return retval_;
	}

	QString errors() const
	{
		return errors_;
	}

protected:
	void run();

private:
	void RunGenRSAKey();
	void RunGenCert();
	void RunGenDHParam();

	bool WriteFile(const QString &name, const QByteArray &data);
	void Fail(const char *what);

	Job job_;
	QString errors_;
	int retval_;
};

void OpenSSLLibWorker::Fail(const char *what)
{
	BIO *mem = BIO_new(BIO_s_mem());

	errors_ += QString::fromLatin1(what);
	errors_ += QLatin1String("\n");

	if (mem)
	{
		ERR_print_errors(mem);
		errors_ += BioToString(mem);
		BIO_free(mem);
	}

	ERR_clear_error();

	retval_ = 1;
}

bool OpenSSLLibWorker::WriteFile(const QString &name, const QByteArray &data)
{
	QFile f(name);

	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		errors_ += QString("Failed to open %1 for writing: %2\n").arg(name).arg(f.errorString());
		retval_ = 1;
		return false;
	}

	if (f.write(data) != data.size() || !f.flush())
	{
		errors_ += QString("Failed to write %1: %2\n").arg(name).arg(f.errorString());
		retval_ = 1;
		return false;
	}

	return true;
}

void OpenSSLLibWorker::run()
{
	retval_ = 1;

	switch (job_)
	{
	case JobGenRSAKey:
		RunGenRSAKey();
		break;

	case JobGenCert:
		RunGenCert();
		break;

	case JobGenDHParam:
		RunGenDHParam();
		break;
	}
}

void OpenSSLLibWorker::RunGenRSAKey()
{
	EVP_PKEY_CTX *ctx = 0;
	EVP_PKEY *pkey = 0;
	BIO *mem = 0;
	const EVP_CIPHER *cipher = 0;
	QByteArray password;
	unsigned char rndchar;
	quint32 numbits;

	/*
	 * Generate random-sized key with min size 4096 and multiply of 8.
	 * Max size is limited to 4096 + 99*8.
	 * See https://blog.josefsson.org/2016/11/03/why-i-dont-use-2048-or-4096-rsa-key-sizes/
	 * "Why I don’t Use 2048 or 4096 RSA Key Sizes"
	 */
	numbits = PICA_RSA_MINKEYSIZE;

	if (RAND_bytes(&rndchar, 1) != 1)
	{
		Fail("Random number generator failure");
		return;
	}

	numbits += 8 * (rndchar % 100);

	/*
	 * What "-rand <file>" did: an extra seeding source, /dev/random when the
	 * account dialog offers it and the user wants it. RAND_load_file() reads
	 * a small fixed amount from anything that is not a regular file, so this
	 * does not sit on the device draining it.
	 */
	if (!rand_.isEmpty())
	{
		if (RAND_load_file(QFile::encodeName(rand_).constData(), -1) <= 0)
		{
			Fail("Failed to seed the random number generator from the requested source");
			return;
		}
	}

	if (setpassword_)
	{
		cipher = EVP_get_cipherbyname(PICA_PRIVKEYENCALGO + 1); /* skip the leading '-' */

		if (!cipher)
		{
			Fail("Unknown private key encryption algorithm " PICA_PRIVKEYENCALGO);
			return;
		}

		/*
		 * UTF-8 rather than the Latin-1 OpenSSLTool wrote down the pipe:
		 * AskPassword hands the passphrase to OpenSSL as UTF-8 when the
		 * account is opened later, so anything outside ASCII produced a key
		 * that could be created but never unlocked again.
		 */
		password = password_.toUtf8();
	}

	ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);

	if (!ctx)
	{
		Fail("Failed to create an RSA key generation context");
		goto end;
	}

	if (EVP_PKEY_keygen_init(ctx) <= 0)
	{
		Fail("Failed to initialize RSA key generation");
		goto end;
	}

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, (int)numbits) <= 0)
	{
		Fail("Failed to set the RSA key size");
		goto end;
	}

	if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
	{
		Fail("Failed to generate the RSA keypair");
		goto end;
	}

	mem = BIO_new(BIO_s_mem());

	if (!mem)
	{
		Fail("Out of memory");
		goto end;
	}

	if (!PEM_write_bio_PrivateKey(mem, pkey, cipher, NULL, 0, NULL,
	                              setpassword_ ? (void*)password.constData() : NULL))
	{
		Fail("Failed to encode the private key");
		goto end;
	}

	if (WriteFile(keyfile_, BioToByteArray(mem)))
		retval_ = 0;

end:

	if (mem)
		BIO_free(mem);

	if (pkey)
		EVP_PKEY_free(pkey);

	if (ctx)
		EVP_PKEY_CTX_free(ctx);
}

void OpenSSLLibWorker::RunGenCert()
{
	EVP_PKEY *pkey = 0;
	X509 *cert = 0;
	X509_NAME *name = 0;
	BIGNUM *serial = 0;
	BIO *keymem = 0;
	BIO *mem = 0;
	const EVP_MD *digest;
	X509V3_CTX extctx;
	QByteArray keydata;
	QByteArray password;
	QByteArray subject;

	/*
	 * The extensions "openssl req -x509" picked up from the [ v3_ca ] section
	 * of openssl.cnf. Spelled out here so that a certificate no longer
	 * depends on which openssl.cnf the machine happens to have - the Windows
	 * build used to have to ship one of its own next to the executable.
	 *
	 * Order matters: "keyid:always" reads the subject key identifier of the
	 * issuer, which for a self signed certificate is the extension added just
	 * above it.
	 */
	static const struct
	{
		int nid;
		const char *value;
	} extensions[] =
	{
		{ NID_subject_key_identifier, "hash" },
		{ NID_authority_key_identifier, "keyid:always,issuer" },
		{ NID_basic_constraints, "critical,CA:true" }
	};

	unsigned int i;

	digest = EVP_get_digestbyname(PICA_CERTDIGESTALGO + 1); /* skip the leading '-' */

	if (!digest)
	{
		Fail("Unknown certificate digest algorithm " PICA_CERTDIGESTALGO);
		return;
	}

	keydata = ReadWholeFile(keyfile_);

	if (keydata.isEmpty())
	{
		errors_ += QString("Failed to read the private key file %1\n").arg(keyfile_);
		retval_ = 1;
		return;
	}

	keymem = BIO_new_mem_buf((void*)keydata.constData(), keydata.size());

	if (!keymem)
	{
		Fail("Out of memory");
		goto end;
	}

	password = password_.toUtf8();

	pkey = PEM_read_bio_PrivateKey(keymem, NULL, NULL, (void*)password.constData());

	if (!pkey)
	{
		Fail("Failed to read the private key");
		goto end;
	}

	cert = X509_new();

	if (!cert)
	{
		Fail("Out of memory");
		goto end;
	}

	if (!X509_set_version(cert, 2)) /* X.509 v3 */
	{
		Fail("Failed to set the certificate version");
		goto end;
	}

	serial = BN_new();

	if (!serial
	        || !BN_rand(serial, PICA_CERT_SERIAL_BITS, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)
	        || !BN_to_ASN1_INTEGER(serial, X509_get_serialNumber(cert)))
	{
		Fail("Failed to generate the certificate serial number");
		goto end;
	}

	name = X509_NAME_new();

	subject = subject_.toUtf8();

	if (!name
	        || !X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_UTF8,
	                                       (unsigned char*)subject.data(), subject.size(), -1, 0))
	{
		Fail("Failed to build the certificate subject name");
		goto end;
	}

	if (!X509_set_subject_name(cert, name) || !X509_set_issuer_name(cert, name))
	{
		Fail("Failed to set the certificate subject name");
		goto end;
	}

	if (!X509_gmtime_adj(X509_getm_notBefore(cert), 0)
	        || !X509_time_adj_ex(X509_getm_notAfter(cert), PICA_CERT_VALIDITY_DAYS, 0, NULL))
	{
		Fail("Failed to set the certificate validity period");
		goto end;
	}

	if (!X509_set_pubkey(cert, pkey))
	{
		Fail("Failed to set the certificate public key");
		goto end;
	}

	X509V3_set_ctx_nodb(&extctx);
	X509V3_set_ctx(&extctx, cert, cert, NULL, NULL, 0);

	for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
	{
		X509_EXTENSION *ext;
		QByteArray value(extensions[i].value);

		ext = X509V3_EXT_conf_nid(NULL, &extctx, extensions[i].nid, value.data());

		if (!ext)
		{
			Fail("Failed to build a certificate extension");
			goto end;
		}

		if (!X509_add_ext(cert, ext, -1))
		{
			X509_EXTENSION_free(ext);
			Fail("Failed to add a certificate extension");
			goto end;
		}

		X509_EXTENSION_free(ext);
	}

	if (!X509_sign(cert, pkey, digest))
	{
		Fail("Failed to sign the certificate");
		goto end;
	}

	mem = BIO_new(BIO_s_mem());

	if (!mem)
	{
		Fail("Out of memory");
		goto end;
	}

	if (!PEM_write_bio_X509(mem, cert))
	{
		Fail("Failed to encode the certificate");
		goto end;
	}

	if (WriteFile(certfile_, BioToByteArray(mem)))
		retval_ = 0;

end:

	if (mem)
		BIO_free(mem);

	if (name)
		X509_NAME_free(name);

	if (serial)
		BN_free(serial);

	if (cert)
		X509_free(cert);

	if (pkey)
		EVP_PKEY_free(pkey);

	if (keymem)
		BIO_free(keymem);
}

void OpenSSLLibWorker::RunGenDHParam()
{
	EVP_PKEY_CTX *ctx = 0;
	EVP_PKEY *pkey = 0;
	BIO *mem = 0;

	ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_DH, NULL);

	if (!ctx)
	{
		Fail("Failed to create a Diffie-Hellman parameter generation context");
		goto end;
	}

	if (EVP_PKEY_paramgen_init(ctx) <= 0)
	{
		Fail("Failed to initialize Diffie-Hellman parameter generation");
		goto end;
	}

	if (EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx, (int)numbits_) <= 0)
	{
		Fail("Failed to set the Diffie-Hellman prime length");
		goto end;
	}

	/* "-5", the generator OpenSSLTool asked the command line program for. */
	if (EVP_PKEY_CTX_set_dh_paramgen_generator(ctx, 5) <= 0)
	{
		Fail("Failed to set the Diffie-Hellman generator");
		goto end;
	}

	if (EVP_PKEY_paramgen(ctx, &pkey) <= 0)
	{
		Fail("Failed to generate the Diffie-Hellman parameters");
		goto end;
	}

	mem = BIO_new(BIO_s_mem());

	if (!mem)
	{
		Fail("Out of memory");
		goto end;
	}

	if (!PEM_write_bio_Parameters(mem, pkey))
	{
		Fail("Failed to encode the Diffie-Hellman parameters");
		goto end;
	}

	if (WriteFile(outfile_, BioToByteArray(mem)))
		retval_ = 0;

end:

	if (mem)
		BIO_free(mem);

	if (pkey)
		EVP_PKEY_free(pkey);

	if (ctx)
		EVP_PKEY_CTX_free(ctx);
}

OpenSSLLib::OpenSSLLib(QObject *parent) :
	QObject(parent), worker_(0)
{
}

OpenSSLLib::~OpenSSLLib()
{
	if (worker_)
	{
		worker_->disconnect(this);
		worker_->wait();
	}
}

static QString NameFromCertBytes(const QByteArray &cert_pem)
{
	X509 *cert;
	QString name;

	cert = CertFromPEM(cert_pem);

	if (!cert)
		return QString();

	name = NameFromSubjectLine(SubjectLine(cert));

	X509_free(cert);

	return name;
}

QString OpenSSLLib::NameFromCertFile(QString cert_file)
{
	return NameFromCertBytes(ReadWholeFile(cert_file));
}

QString OpenSSLLib::NameFromCertString(QString cert_pem)
{
	return NameFromCertBytes(cert_pem.toLatin1());
}

QString OpenSSLLib::CertTextFromString(QString cert_pem)
{
	X509 *cert;
	BIO *mem;
	QString text;

	cert = CertFromPEM(cert_pem.toLatin1());

	if (!cert)
		return QString();

	mem = BIO_new(BIO_s_mem());

	if (!mem)
	{
		X509_free(cert);
		return QString();
	}

	if (X509_print_ex(mem, cert, MULTILINE_NAMEOPT, 0))
		text = BioToString(mem);

	BIO_free(mem);
	X509_free(cert);
	ERR_clear_error();

	return text;
}

bool OpenSSLLib::StartWorker(OpenSSLLibWorker *worker, QObject *receiver, const char *finished_slot)
{
	if (worker_)
	{
		delete worker;
		return false;
	}

	worker_ = worker;

	errors_.clear();
	output_.clear();

	disconnect(this, SIGNAL(finished(int)), 0, 0);
	connect(this, SIGNAL(finished(int)), receiver, finished_slot);

	/*
	 * Queued, because QThread::finished() is emitted on the worker thread and
	 * this object lives on the one that called - so WorkerFinished(), and the
	 * receiver's slot after it, run where the caller expects them to.
	 */
	connect(worker_, SIGNAL(finished()), this, SLOT(WorkerFinished()));

	worker_->start();

	return true;
}

void OpenSSLLib::WorkerFinished()
{
	OpenSSLLibWorker *worker = worker_;
	int retval;

	if (!worker)
		return;

	retval = worker->retval();
	errors_ = worker->errors();

	worker_ = 0;
	worker->wait(); /* returns at once - run() is over by the time this is delivered */
	worker->deleteLater();

	/*
	 * Last thing done here: the receiver is allowed to delete us from its
	 * slot, the way DHParam disposes of its generator.
	 */
	emit finished(retval);
}

bool OpenSSLLib::GenRSAKeySignal(QString keyfile, bool setpassword, QString password, QString rand, QObject *receiver, const char *finished_slot)
{
	OpenSSLLibWorker *worker = new OpenSSLLibWorker(OpenSSLLibWorker::JobGenRSAKey, this);

	worker->keyfile_ = keyfile;
	worker->setpassword_ = setpassword;
	worker->password_ = password;
	worker->rand_ = rand;

	return StartWorker(worker, receiver, finished_slot);
}

bool OpenSSLLib::GenCertSignal(QString cert_file, QString keyfile, QString keypassword,
                               QString subject, QObject *receiver, const char *finished_slot)
{
	OpenSSLLibWorker *worker = new OpenSSLLibWorker(OpenSSLLibWorker::JobGenCert, this);

	worker->certfile_ = cert_file;
	worker->keyfile_ = keyfile;
	worker->password_ = keypassword;
	worker->subject_ = subject;

	return StartWorker(worker, receiver, finished_slot);
}

bool OpenSSLLib::GenDHParamSignal(quint32 numbits, QString output_file, QObject *receiver, const char *finished_slot)
{
	OpenSSLLibWorker *worker = new OpenSSLLibWorker(OpenSSLLibWorker::JobGenDHParam, this);

	worker->numbits_ = numbits;
	worker->outfile_ = output_file;

	return StartWorker(worker, receiver, finished_slot);
}

QString OpenSSLLib::ReadStdErr()
{
	QString errors = errors_;

	errors_.clear();

	return errors;
}

QString OpenSSLLib::ReadStdOut()
{
	QString output = output_;

	output_.clear();

	return output;
}
