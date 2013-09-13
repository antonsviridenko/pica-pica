#include "openssltool.h"
#include <QProcess>
#include <QRegExp>
#include <QMessageBox>

OpenSSLTool::OpenSSLTool()
{
}

QString OpenSSLTool::NameFromCertFile(QString cert_file)
{
    //
    QProcess openssl;

    openssl.start("openssl",QStringList()<<"x509"<<"-in"<<cert_file<<"-subject"<<"-nameopt"<<"oneline,-esc_msb,utf8"<<"-noout");
    if (!openssl.waitForStarted())
        return QString();

    if (!openssl.waitForFinished())
        return QString();

    QByteArray result = openssl.readAll();
        QRegExp rxOld("CN = [0-9]+\\#([^\\s\\#]+)");
    QRegExp rx("CN = ([^\\s\\#]+)");

    QString name=QString::fromUtf8( result.constData());

    if (name.contains(rxOld))
    {
    name=rxOld.cap(1);
    }
    else if (name.contains(rx))
    {
    name=rx.cap(1);
    }

    //
return name;
}

QString OpenSSLTool::NameFromCertString(QString cert_pem)
{
    QProcess openssl;

    openssl.start("openssl",QStringList()<<"x509"<<"-subject"<<"-nameopt"<<"oneline,-esc_msb,utf8"<<"-noout");
    if (!openssl.waitForStarted())
        return QString();

    openssl.write(cert_pem.toAscii());

    if (!openssl.waitForFinished())
        return QString();

    QByteArray result = openssl.readAll();
    QRegExp rxOld("CN = [0-9]+\\#([^\\s\\#]+)");
    QRegExp rx("CN = ([^\\s\\#]+)");

    QString name=QString::fromUtf8( result.constData());

    if (name.contains(rxOld))
    {
    name=rxOld.cap(1);
    }
    else if (name.contains(rx))
    {
    name=rx.cap(1);
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

    openssl.start("openssl", QStringList()<<"x509"<<"-noout"<<"-text"<<"-nameopt"<<"multiline,-esc_msb,utf8");

    if (!openssl.waitForStarted())
        return QString();

    ba = cert_pem.toAscii();
    ret = openssl.write(data = (char*)ba.constData());
    openssl.closeWriteChannel();

    if (!openssl.waitForFinished())
        return QString();

    return QString::fromUtf8(openssl.readAllStandardOutput().constData());
}

bool OpenSSLTool::GenRSAKeySignal(quint32 numbits, QString keyfile, bool setpassword, QString password, QObject *receiver, const char *finished_slot)
{
    if (!setpassword)
    {
        openssl_.start("openssl", QStringList()<<"genrsa"<<"-out"<<keyfile<<QString::number(numbits));
    }
    else
    {
        openssl_.start("openssl", QStringList()<<"genrsa"<<"-out"<<keyfile<<"-passout"<<"stdin"<<"-idea"<<QString::number(numbits));
    }

    if (!openssl_.waitForStarted())
        return false;

    if (setpassword)
    {
        openssl_.write((password + "\n").toAscii().constData());
    }

    openssl_.disconnect(&openssl_, SIGNAL(finished(int,QProcess::ExitStatus)), 0, 0);
    openssl_.connect(&openssl_, SIGNAL(finished(int,QProcess::ExitStatus)),receiver, finished_slot);

    return true;
}

bool OpenSSLTool::GenCertSignal(QString cert_file, QString keyfile, QString keypassword,
                                    QString subject, QObject *receiver, const char *finished_slot)
{
    openssl_.start("openssl",
                   QStringList()
                        <<"req"
#ifdef WIN32
						<<"-config"<<"openssl.cnf"
#endif
                        <<"-out"<<cert_file
                        <<"-key"<<keyfile
                        <<"-subj"<<"/CN=" + subject
                        <<"-new"
                        <<"-x509"
                        <<"-days"<<"3680"
                        <<"-utf8"
                        <<"-batch"
                        <<"-passin"
                        <<"stdin");

    if (!openssl_.waitForStarted())
        return false;

    openssl_.write((keypassword + "\n").toAscii().constData());

    openssl_.disconnect(&openssl_, SIGNAL(finished(int,QProcess::ExitStatus)), 0, 0);
    openssl_.connect(&openssl_, SIGNAL(finished(int,QProcess::ExitStatus)),receiver, finished_slot);

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

bool OpenSSLTool::GenDHParam(quint32 numbits, QString output_file)
{//openssl dhparam -5 4096
    openssl_.start("openssl",
                   QStringList()
                        <<"dhparam"
                        <<"-out"<<output_file
                        <<"-5"
                        <<QString::number(numbits));

    if (!openssl_.waitForStarted())
        return false;

    if (!openssl_.waitForFinished(-1))
        return false;

    if (openssl_.exitCode() != 0)
        return false;

    return true;
}


