#ifndef REGISTERACCOUNTDIALOG_H
#define REGISTERACCOUNTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QRegExpValidator>
#include "../openssltool.h"

class RegisterAccountDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RegisterAccountDialog(QWidget *parent = 0);
    QString GetCertFilename();
    QString GetPkeyFilename();
    
private:
    //QPushButton *btOK;
    QPushButton *btRegister;
    QLineEdit *nickname;
    QLabel *lbStatus;
    QCheckBox *cbSetPassword;
    QLineEdit *password;
    QLineEdit *repeatPassword;
    QLabel *lbPassword ;
    QLabel *lbRepeatPassword;
    QCheckBox *cbUseDevRandom;

    QRegExpValidator *vld;
    OpenSSLTool ost;
    QByteArray cert_buf;

    QString CertFilename_;
    QString PkeyFilename_;
signals:
    
private slots:
    //void OK();
    void Register();
    void setPasswordClick();
    void useDevRandomClick();

    void stageSignCert(int retval, QProcess::ExitStatus);
    void stageFinished(int retval, QProcess::ExitStatus);
};

#endif // REGISTERACCOUNTDIALOG_H
