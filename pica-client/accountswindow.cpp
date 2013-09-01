#include "accountswindow.h"
#include <QVBoxLayout>
#include "mainwindow.h"
#include "globals.h"
#include "dialogs/addaccountdialog.h"
#include "dialogs/registeraccountdialog.h"
#include "../PICA_client.h"
#include "skynet.h"
#include "openssltool.h"
#include <QMessageBox>
#include <QApplication>
#include <cstring>

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

         f = cb_accounts->font();
         f.setStyleHint(QFont::Monospace);
         f.setFixedPitch(true);
         f.setBold(true);
         f.setFamily("Liberation Mono");
         cb_accounts->setFont(f);
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
            cb_accounts->addItem(QString("%1 (%2...)").arg(L[i].name).arg(L[i].id.toBase64().left(8).constData()),L[i].id);

        bt_login->setEnabled(true);
    }
}

void AccountsWindow::login_click()
{
    int L_index;

    for (int i=0;i<L.count();i++)
    {
        if (L[i].id == cb_accounts->itemData(cb_accounts->currentIndex()))
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

    Accounts::SetCurrentAccount(L[L_index]);

    skynet->Join(L[L_index]);

    mainwindow = new MainWindow();
    mainwindow->show();
    this->hide();

}

void AccountsWindow::CreateAccount(QString CertFilename, QString PkeyFilename, bool copyfiles)
{
    QByteArray id(PICA_ID_SIZE, 0);
    int ret;

    ret = PICA_get_id_from_cert_file(CertFilename.toUtf8().constData(), (unsigned char*)id.data());

    if (0 == ret)
    {
        QMessageBox mbx;
        mbx.setText("Invalid certificate");
        mbx.exec();
        return;
    }

    QString name = OpenSSLTool::NameFromCertFile(CertFilename);

    Accounts::AccountRecord rec;
    QString certfilename, pkeyfilename;

    if (copyfiles)
    {
        QDir dir(config_dir);
        dir.mkdir(name + id.toHex().constData());
        dir.cd(name + id.toHex().constData());

        certfilename=dir.absolutePath()+"/" + id.toHex().constData() + "_cert.pem";
        pkeyfilename=dir.absolutePath()+"/" + id.toHex().constData() + "_pkey.pem";

        QFile::copy(CertFilename,certfilename);
        QFile::copy(PkeyFilename,pkeyfilename);
    }
    else
    {
        certfilename = CertFilename;
        pkeyfilename = PkeyFilename;
    }

    rec.id=id;
    rec.name=name;
    rec.cert_file=certfilename;
    rec.pkey_file=pkeyfilename;
    rec.CA_file= certfilename;//config_defaultCA;

    accounts.Add(rec);

    if (!accounts.isOK())
    {
        QMessageBox mbx;
        mbx.setText(accounts.GetLastError());
        mbx.exec();
    }
    LoadAccounts();
}

void AccountsWindow::register_click()
{
    RegisterAccountDialog d;
    d.exec();

    if (d.result())
    {
        CreateAccount(d.GetCertFilename(), d.GetPkeyFilename(), true);

        QFile::remove(d.GetCertFilename());
        QFile::remove(d.GetPkeyFilename());
    }
}

void AccountsWindow::add_click()
{
    AddAccountDialog d;
    d.exec();

    if (d.result())
    {
        CreateAccount(d.GetCertFilename(), d.GetPkeyFilename(), d.isCopyFilesChecked());
    }
}

void AccountsWindow::delete_click()
{
    QByteArray id = (cb_accounts->itemData(cb_accounts->currentIndex())).toByteArray();

    if (QMessageBox::No==
            QMessageBox::question(this,tr("Deleting account"),tr("Are you sure you want to delete account %1 ?").arg(id.toBase64().constData()),QMessageBox::Yes,QMessageBox::No))
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
