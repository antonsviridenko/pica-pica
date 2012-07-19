#include "accountswindow.h"
#include <QVBoxLayout>
#include "mainwindow.h"
#include "globals.h"
#include "dialogs/addaccountdialog.h"
#include "dialogs/registeraccountdialog.h"
#include "../PICA_client.h"
#include "skynet.h"
#include <QRegExp>
#include <QMessageBox>
#include <QApplication>

AccountsWindow::AccountsWindow(QWidget *parent) :
    QMainWindow(parent),
    accounts(config_dbname)
{
     QVBoxLayout *layout;
     layout = new QVBoxLayout;

     cb_accounts = new QComboBox();
     bt_login = new QPushButton(tr("Log In"));
     bt_registernew = new QPushButton(tr("Register New Account..."));
     bt_addimport = new QPushButton(tr("Add (Import) existing account..."));
     bt_delete = new QPushButton(tr("Delete Account"));

     {
         QFont f;
         f= bt_login->font();
         f.setBold(true);
         bt_login->setFont(f);
     }

     layout->addWidget(cb_accounts);
     layout->addWidget(bt_login);
     layout->addWidget(bt_registernew);
     layout->addWidget(bt_addimport);
     layout->addWidget(bt_delete);

     QWidget *central = new QWidget(this);
     setCentralWidget(central);

     centralWidget()->setLayout(layout);

     setWindowIcon(picapica_ico_sit);

     connect(bt_login,SIGNAL(clicked()),this,SLOT(login_click()));
     connect(bt_registernew,SIGNAL(clicked()),this,SLOT(register_click()));
     connect(bt_addimport,SIGNAL(clicked()),this,SLOT(add_click()));
     connect(bt_delete,SIGNAL(clicked()),this,SLOT(delete_click()));

    LoadAccounts();
}

void AccountsWindow::LoadAccounts()
{
    cb_accounts->clear();
    L.clear();
    L=accounts.GetAccounts();

    if (0==L.count())
    {
        cb_accounts->addItem(tr(" ---No Accounts --"));

        bt_login->setDisabled(true);
    }
    else
    {
        for (int i=0;i<L.count();i++)
            cb_accounts->addItem(QString("(%1) %2").arg(L[i].id).arg(L[i].name),L[i].id);

        bt_login->setEnabled(true);
    }
}

void AccountsWindow::login_click()
{

    account_id = (cb_accounts->itemData(cb_accounts->currentIndex())).toUInt();

    int L_index;

    for (int i=0;i<L.count();i++)
    {
        if (L[i].id==account_id)
        {
            L_index = i;
            break;
        }
    }

    {
      QString error_str;
      if (!Accounts::CheckFiles(L[L_index],error_str))
      {
        QMessageBox mbx;
        mbx.setText(error_str);
        mbx.exec();
        return;
      }
    }
    skynet->Join(L[L_index]);

    mainwindow = new MainWindow();
    mainwindow->show();
    this->hide();

}

void AccountsWindow::register_click()
{
    RegisterAccountDialog d;
    d.exec();
}

void AccountsWindow::add_click()
{
    AddAccountDialog d;
    d.exec();

    if (d.result())
    {
    quint32 id;

    PICA_get_id_from_cert(d.GetCertFilename().toUtf8().constData(),&id);

    //
    QProcess openssl;

    openssl.start("openssl",QStringList()<<"x509"<<"-in"<<d.GetCertFilename()<<"-subject"<<"-nameopt"<<"oneline,-esc_msb,utf8"<<"-noout");
    if (!openssl.waitForStarted())
        return;

    if (!openssl.waitForFinished())
        return;

    QByteArray result = openssl.readAll();
    QRegExp rx("CN = [0-9]+\\#([^\\s\\#]+)");


    QString name=QString::fromUtf8( result.constData());

    if (name.contains(rx))
    {
    name=rx.cap(1);
    }

    //
    Accounts::AccountRecord rec;
    QString certfilename,pkeyfilename;

    if (d.isCopyFilesChecked())
    {
        QDir dir(config_dir);
        dir.mkdir(QString::number(id));
        dir.cd(QString::number(id));

        certfilename=dir.absolutePath()+"/"+QString::number(id)+"_cert.pem";
        pkeyfilename=dir.absolutePath()+"/"+QString::number(id)+"_pkey.pem";

        QFile::copy(d.GetCertFilename(),certfilename);
        QFile::copy(d.GetPkeyFilename(),pkeyfilename);
    }
    else
    {
        certfilename = d.GetCertFilename();
        pkeyfilename = d.GetPkeyFilename();
    }

    rec.id=id;
    rec.name=name;
    rec.cert_file=certfilename;
    rec.pkey_file=pkeyfilename;
    rec.CA_file= config_defaultCA;

    accounts.Add(rec);

    if (!accounts.isOK())
    {
        QMessageBox mbx;
        mbx.setText(accounts.GetLastError());
        mbx.exec();
    }
    LoadAccounts();
    }
}

void AccountsWindow::delete_click()
{
    quint32 id = (cb_accounts->itemData(cb_accounts->currentIndex())).toUInt();

    if (QMessageBox::No==
            QMessageBox::question(this,tr("Deleting account"),tr("Are you sure you want to delete account %1 ?").arg(id),QMessageBox::Yes,QMessageBox::No))
        return;

    //TODO ask user if he wants to wipe certificate and private key too

    accounts.Delete(id);

    LoadAccounts();

}

void AccountsWindow::closeEvent(QCloseEvent *e)
{
    QApplication::instance()->quit();
    QMainWindow::closeEvent(e);
}
