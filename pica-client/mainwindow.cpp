#include "mainwindow.h"
#include "globals.h"
#include "skynet.h"
#include "picaactioncenter.h"
#include "accounts.h"
#include "ui_mainwindow.h"
#include <QVBoxLayout>
#include <QMessageBox>


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QVBoxLayout *layout;

    layout = new QVBoxLayout;
    contact_list = new ContactListWidget;
    status = new QComboBox;
    notifications = new QListWidget();
    bt_showhide_notifications = new QPushButton();
    hide_notifications();

    status->setMinimumHeight(36);
    notifications->setMinimumHeight(48);

    layout->addWidget(contact_list, 8);
    layout->addWidget(bt_showhide_notifications, 0, Qt::AlignRight);
    layout->addWidget(notifications, 1, Qt::AlignBottom);
    layout->addWidget(status,0,Qt::AlignBottom);

    centralWidget()->setLayout(layout);
    setWindowIcon(picapica_ico_sit);

    createMenus();


    //for (int i=0;i<255;i++)
    //    contact_list->addItem(QString("test %1").arg(i));

    status->addItem(picapica_ico_fly, QString(tr("Connected")));
    status->addItem(picapica_ico_sit, QString(tr("Disconnected")));

    connect(skynet, SIGNAL(BecameSelfAware()), this, SLOT(set_online()));
    connect(skynet, SIGNAL(LostSelfAwareness()), this, SLOT(set_offline()));
    connect(status, SIGNAL(currentIndexChanged(int)), this, SLOT(status_changed(int)));
    connect(bt_showhide_notifications, SIGNAL(clicked()), this, SLOT(showhide_click()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::set_offline()
{
    SetStatus(false);
}

void MainWindow::set_online()
{
    SetStatus(true);
}

void MainWindow::status_changed(int index)
{
    if (status_change_disable_flag)
        return;

    if (index == 0 && !skynet->isSelfAware())
    {
        Accounts::AccountRecord curr_acc = skynet->CurrentAccount();
        skynet->Join(curr_acc);
    }
    if (1 == index && skynet->isSelfAware())
        skynet->Exit();
}

void MainWindow::SetStatus(bool connected)
{
    status_change_disable_flag = true;

    if (connected)
    {
        status->setCurrentIndex(0);
        setWindowTitle(tr("Pica Pica Messenger - ") + QString::number(account_id));
    }
    else
    {
        status->setCurrentIndex(1);
        setWindowTitle(tr("Pica Pica Messenger - Disconnected"));
    }

    status_change_disable_flag = false;
}


void MainWindow::createMenus()
{
    contactsMenu=menuBar()->addMenu(tr("&Contacts"));
    contactsMenu->addAction(contact_list->addcontactAct);
    contactsMenu->addSeparator();
    contactsMenu->addAction(action_center->ExitAct());

    nodesMenu=menuBar()->addMenu(tr("&Nodes"));

    helpMenu=menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(action_center->AboutAct());

}

void MainWindow::show_notifications()
{
    notifications->show();
    bt_showhide_notifications->setText(tr("Hide Notifications"));
    status_notifications_hidden = false;
}

void MainWindow::hide_notifications()
{
    notifications->hide();
    bt_showhide_notifications->setText(tr("Show Notifications"));
    status_notifications_hidden = true;
}

void MainWindow::showhide_click()
{
    if (status_notifications_hidden)
        show_notifications();
    else
        hide_notifications();
}

void MainWindow::AddNotification(QString &text)
{
    notifications->addItem(text);
    notifications->item(notifications->count() -1) ->setForeground(QBrush(QColor(230, 10, 10, 196)));
    show_notifications();
}



