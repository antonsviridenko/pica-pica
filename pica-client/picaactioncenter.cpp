#include "picaactioncenter.h"
#include "globals.h"
#include "mainwindow.h"
#include "skynet.h"
#include <QApplication>
#include "../PICA_proto.h"

#include <QMessageBox>

PicaActionCenter::PicaActionCenter(QObject *parent) :
    QObject(parent)
{
    exitAct = new QAction(tr("E&xit"),this);
    exitAct->setStatusTip(tr("Close Pica Pica messenger and exit"));
    connect(exitAct,SIGNAL(triggered()),this,SLOT(exit()));

    aboutAct = new QAction(tr("A&bout"),this);
    aboutAct->setStatusTip(tr("About Pica Pica Messenger"));
    connect(aboutAct,SIGNAL(triggered()),this,SLOT(about()));
}

void PicaActionCenter::about()
{
#ifdef PACKAGE_VERSION
#define VERSION_STRING "v"PACKAGE_VERSION
#else
#define VERSION_STRING ""
#endif
    QMessageBox mbx;
    mbx.setWindowIcon(picapica_ico_sit);
    mbx.setIconPixmap(picapica_ico_sit.pixmap(32));
    mbx.setText(tr("Pica Pica Messenger "VERSION_STRING"\n(c) 2012 Anton Sviridenko\n\
http://picapica.im\n\nContributors:\nDaniil Ustinov - bugfixes\n\n\
protocol version " PICA_PROTO_VER_STRING));
    mbx.exec();
}

void PicaActionCenter::exit()
{
    if (mainwindow)
        mainwindow->close();

    skynet->Exit();
    QApplication::instance()->quit();
}
