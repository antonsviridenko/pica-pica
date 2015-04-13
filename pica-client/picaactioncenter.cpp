#include "picaactioncenter.h"
#include "globals.h"
#include "mainwindow.h"
#include "skynet.h"
#include <QApplication>
#include "../PICA_proto.h"
#include "accounts.h"
#include "dialogs/showpicaiddialog.h"
#include "sound.h"

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

    showmyidAct = new QAction(tr("Show My Pica Pica ID"), this);
    connect(showmyidAct, SIGNAL(triggered()), this, SLOT(showmyid()));

    muteSoundsAct = new QAction(tr("&Mute Sounds"), this);
    muteSoundsAct->setCheckable(true);
    connect(muteSoundsAct, SIGNAL(triggered(bool)), this, SLOT(muteSounds(bool)));
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
    mbx.setTextFormat(Qt::RichText);
    mbx.setText(tr("<b>Pica Pica Messenger "VERSION_STRING"<br>(c) 2012 - 2015 Anton Sviridenko</b><br>\
<a href=https://picapica.im>https://picapica.im</a><br><br>Contributors:<br>Daniil Ustinov - bugfixes<br>EXL - bugfixes<br><br>\
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

void PicaActionCenter::showmyid()
{
    ShowPicaIdDialog d(Accounts::GetCurrentAccount().name, Accounts::GetCurrentAccount().id, tr("My Current Account"));
    d.exec();
}

void PicaActionCenter::muteSounds(bool yes)
{
    Sound::mute(yes);
}
