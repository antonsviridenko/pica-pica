#ifndef REGISTERACCOUNTDIALOG_H
#define REGISTERACCOUNTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>

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

signals:
    
public slots:
    void OK();
    void Register();
};

#endif // REGISTERACCOUNTDIALOG_H
