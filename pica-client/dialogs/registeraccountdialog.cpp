#include "registeraccountdialog.h"
#include "globals.h"
#include <QVBoxLayout>
#include <QDir>
#include <QMessageBox>


RegisterAccountDialog::RegisterAccountDialog(QWidget *parent) :
    QDialog(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *lbNickname = new QLabel(tr("Name:"));
    nickname = new QLineEdit();
    btRegister = new QPushButton(tr("Register"));
    lbStatus = new QLabel(tr("Enter your name and press\n \"Register\" to get certificate"));
    btOK = new QPushButton(tr("OK"));

    {
        QFont f;
        f= lbStatus->font();
        f.setBold(true);
        lbStatus->setFont(f);
    }

    layout->addWidget(lbNickname);
    layout->addWidget(nickname);
    layout->addWidget(btRegister);
    layout->addWidget(lbStatus);
    layout->addWidget(btOK);

    setLayout(layout);

    vld = new QRegExpValidator(QRegExp("[^\\#/=\"\'\\$]+"), this);
    nickname->setValidator(vld);

    connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));
    connect(btRegister, SIGNAL(clicked()), this, SLOT(Register()));
}

void RegisterAccountDialog::OK()
{

    done(0);
}

void RegisterAccountDialog::Register()
{
    //check name
    QString name = nickname->text();

    if (name.toUtf8().size() > 52) //64bytes -field size in x509 certificate, - 10 for number, -1 for #, 1 for terminating zero
    {
        QMessageBox mbx;
        mbx.setText(tr("Name is too long"));
        mbx.exec();
        return;
    }

    //1
    lbStatus->setText(tr("1) generating RSA keypair..."));

    nickname->setEnabled(false);
    btRegister->setEnabled(false);

    ost.GenRSAKeySignal(4096, config_dir + QDir::separator() + "privkey.pem", this, SLOT(stageGenCSR(int, QProcess::ExitStatus)));
}

void RegisterAccountDialog::stageGenCSR(int retval, QProcess::ExitStatus)
{
    if (retval != 0)
    {
        lbStatus->setText(tr("Error:\n") + ost.ReadStdErr());
        return;
    }

    lbStatus->setText(tr("2) Generating CSR..."));

    ost.GenCSRSignal(config_dir + QDir::separator() + "csr.pem",
                     config_dir + QDir::separator() + "privkey.pem",
                     nickname->text(),
                     this,
                     SLOT(stageConnect(int,QProcess::ExitStatus)));
}

void RegisterAccountDialog::stageConnect(int retval,QProcess::ExitStatus)
{
    static const char *registrar_hosts[] = {"registrar.picapica.im","registrar.picapica.ge"};
    static unsigned char registrar_index;

    if (retval != 0)
    {
        lbStatus->setText(tr("Error:\n") + ost.ReadStdErr());
        return;
    }

    lbStatus->setText(tr("3) Connecting to server..."));

    connect(&sock, SIGNAL(connected()), this, SLOT(stageGetCert()));
    connect(&sock, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(showError()));

    sock.connectToHost(registrar_hosts[(registrar_index++)%(sizeof(registrar_hosts)/sizeof(char*))], 2299);
}

void RegisterAccountDialog::stageGetCert()
{
    lbStatus->setText(tr("4) Connected. Waiting for certificate..."));

    QFile csr(config_dir + QDir::separator() + "csr.pem");
    csr.open(QIODevice::ReadOnly);

    QByteArray buf;

    buf = csr.readAll();

    sock.write(buf.data(), buf.size());
    connect(&sock, SIGNAL(readyRead()), this, SLOT(stageReadCert()));

    cert_buf.clear();
}

void RegisterAccountDialog::stageReadCert()
{
    cert_buf.append(sock.readAll());

    if (cert_buf.contains("-----END CERTIFICATE-----") || cert_buf.contains("-----END X509 CERTIFICATE-----"))
    {
        disconnect(&sock, SIGNAL(error(QAbstractSocket::SocketError)), 0, 0);

        QFile cert(config_dir + QDir::separator() + "cert.pem");
        cert.open(QIODevice::WriteOnly | QIODevice::Truncate);
        cert.write(cert_buf.data(),cert_buf.size());
        cert.close();

        QFile::remove(config_dir + QDir::separator() + "csr.pem");

        CertFilename_ = config_dir + QDir::separator() + "cert.pem";
        PkeyFilename_ = config_dir + QDir::separator() + "privkey.pem";

        done(1);
    }
}

QString RegisterAccountDialog::GetCertFilename()
{
    return CertFilename_;
}

QString RegisterAccountDialog::GetPkeyFilename()
{
    return PkeyFilename_;
}

void RegisterAccountDialog::showError()
{
  QMessageBox mbx;
  mbx.setText(sock.errorString());
  mbx.exec();
}
