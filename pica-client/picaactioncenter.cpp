#include "picaactioncenter.h"
#include "globals.h"
#include "mainwindow.h"
#include "skynet.h"
#include <QApplication>
#include <QMessageBox>
#include <QPalette>
#include <QDesktopServices>
#include <QEvent>
#include <QMouseEvent>

PicaActionCenter::PicaActionCenter(QObject *parent) :
    QObject(parent)
{
    exitAct = new QAction(tr("E&xit"),this);
    exitAct->setStatusTip(tr("Close Pica Pica messenger and exit"));
    connect(exitAct,SIGNAL(triggered()),this,SLOT(exit()));

    aboutAct = new QAction(tr("A&bout"),this);
    aboutAct->setStatusTip(tr("About Pica Pica Messenger"));
    connect(aboutAct,SIGNAL(triggered()),this,SLOT(about()));
    link = new QLabel;
    link->installEventFilter(this);
}

bool PicaActionCenter::eventFilter(QObject *obj, QEvent *ev)
{
    QMouseEvent *mEvent = (QMouseEvent*)ev;

    if (obj == link && mEvent->button() == Qt::LeftButton &&
            ev->type() == QEvent::MouseButtonRelease) {
        QDesktopServices::openUrl(QUrl(link->text()));
    }

    ev->accept();
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
    QString author = "Pica Pica Messenger "VERSION_STRING"\n"
                     "(c) 2012 Anton Sviridenko";

    link->setParent(&mbx);
    link->setText("http://picapica.im");
    int x = author.count("\n") + 1;
    float f = mbx.font().pointSize();
    link->move(60, f * 2 * x);
    link->resize(150, 30);
    link->setForegroundRole(QPalette::Link);
    QCursor cursor;
    cursor.setShape(Qt::PointingHandCursor);
    link->setCursor(cursor);

    QString contrib = "\n\n\nContributors:\nDaniil Ustinov - bugfixes";
    mbx.setText(tr(QString(author + contrib).toStdString().c_str()));

    mbx.exec();
}

void PicaActionCenter::exit()
{
    if (mainwindow)
        mainwindow->close();

    skynet->Exit();
    QApplication::instance()->quit();
}
