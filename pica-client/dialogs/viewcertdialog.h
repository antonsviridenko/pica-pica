#ifndef VIEWCERTDIALOG_H
#define VIEWCERTDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>

class ViewCertDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ViewCertDialog(QWidget *parent = 0);
    void SetCert(QString cert_pem);
    
private:
    QTextEdit *cert_text;
    QPushButton *btOK;

signals:
    
public slots:

private slots:
    void OK();
    
};

#endif // VIEWCERTDIALOG_H
