#ifndef ADDACCOUNTDIALOG_H
#define ADDACCOUNTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QRadioButton>

class AddAccountDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AddAccountDialog(QWidget *parent = 0);
    QString GetCertFilename();
    QString GetPkeyFilename();
    bool isCopyFilesChecked();

private:
    QPushButton *btOK;
    QPushButton *btCancel;

    QLineEdit *cert_filename;
    QLineEdit *pkey_filename;

    QPushButton *bt_browse_cert;
    QPushButton *bt_browse_pkey;

    QRadioButton *rb_copyfiles;
    QRadioButton *rb_readinplace;
signals:

public slots:
private slots:
    void OK();
    void Cancel();
    void browse_cert();
    void browse_pkey();

};

#endif // ADDACCOUNTDIALOG_H
