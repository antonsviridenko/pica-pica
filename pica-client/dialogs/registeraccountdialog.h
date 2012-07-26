#ifndef REGISTERACCOUNTDIALOG_H
#define REGISTERACCOUNTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QRegExpValidator>
#include <QtNetwork>
#include <QTcpSocket>
#include "openssltool.h"

class RegisterAccountDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegisterAccountDialog(QWidget *parent = 0);
    QString GetCertFilename();
    QString GetPkeyFilename();
    
private:
    QPushButton *btOK;
    QPushButton *btRegister;
    QLineEdit *nickname;
    QLabel *lbStatus;

    QRegExpValidator *vld;
    OpenSSLTool ost;
    QTcpSocket sock;
    QByteArray cert_buf;

    QString CertFilename_;
    QString PkeyFilename_;
signals:
    
public slots:
    void OK();
    void Register();

    void stageGenCSR(int,QProcess::ExitStatus);
    void stageConnect(int,QProcess::ExitStatus);
    void stageGetCert();
    void stageReadCert();
    void showError();
};

#endif // REGISTERACCOUNTDIALOG_H
