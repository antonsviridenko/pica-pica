#ifndef PICASYSTRAY_H
#define PICASYSTRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include "skynet.h"
#include "mainwindow.h"
#include "globals.h"

class PicaSysTray : public QObject
{
    Q_OBJECT
public:
    explicit PicaSysTray(QObject *parent = 0);
    
signals:
    
public slots:

private:
    QSystemTrayIcon *systray_;
    QMenu *systrayMenu_;

private slots:
    void systray_activated(QSystemTrayIcon::ActivationReason);
    void skynet_became_self_aware();//debug
    void skynet_lost_self_awareness();//debug
};

#endif // PICASYSTRAY_H
