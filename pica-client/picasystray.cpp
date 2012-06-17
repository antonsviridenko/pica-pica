#include "picasystray.h"
#include "globals.h"
#include "picaactioncenter.h"

PicaSysTray::PicaSysTray(QObject *parent) :
    QObject(parent)
{
    systrayMenu_ = new QMenu();

    systrayMenu_->addAction(action_center->AboutAct());
    systrayMenu_->addSeparator();
    systrayMenu_->addAction(action_center->ExitAct());

    systray_ = new QSystemTrayIcon(picapica_ico_sit);
    systray_->setContextMenu(systrayMenu_);
    systray_->show();

    connect(systray_, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(systray_activated(QSystemTrayIcon::ActivationReason)));
    connect(skynet, SIGNAL(BecameSelfAware()), this, SLOT(skynet_became_self_aware()));
    connect(skynet, SIGNAL(LostSelfAwareness()), this, SLOT(skynet_lost_self_awareness()));
}

void PicaSysTray::systray_activated(QSystemTrayIcon::ActivationReason r)
{
    if (r == QSystemTrayIcon::DoubleClick && mainwindow!=NULL)
    {
        mainwindow->showNormal();
    }
}

void PicaSysTray::skynet_became_self_aware()
{
    systray_->setIcon(picapica_ico_fly);
}

void PicaSysTray::skynet_lost_self_awareness()
{
    systray_->setIcon(picapica_ico_sit);
}
