#ifndef PICASYSTRAY_H
#define PICASYSTRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QPixmap>
#include "skynet.h"
#include "mainwindow.h"
#include "globals.h"

class PicaSysTray : public QObject
{
	Q_OBJECT
public:
	explicit PicaSysTray(QObject *parent = 0);
	void StartBlinking();
	void StopBlinking();

signals:
	void doubleclicked();

public slots:

private:
	QSystemTrayIcon *systray_;
	QMenu *systrayMenu_;
	void timerEvent(QTimerEvent *event);
	int timer_id_;
	QIcon blink_;
	QPixmap emptyicon_;

private slots:
	void systray_activated(QSystemTrayIcon::ActivationReason);
	void skynet_became_self_aware();//debug
	void skynet_lost_self_awareness();//debug
};

#endif // PICASYSTRAY_H
