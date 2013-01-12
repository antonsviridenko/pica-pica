#include <QtGui/QApplication>
#include "mainwindow.h"
#include "accountswindow.h"
#include "globals.h"
#include "contacts.h"
#include "skynet.h"
#include "msguirouter.h"
#include "picaactioncenter.h"
#include "picasystray.h"
#include <QDir>
#include <QMessageBox>
#include <QString>
#include <QIcon>

//globals
QString config_dir;
QString config_dbname;
SkyNet *skynet;
MainWindow *mainwindow;
unsigned int account_id;
class AskPassword *askpassword;
class MsgUIRouter *msguirouter;

QIcon picapica_ico_sit;
QIcon picapica_ico_fly;

class PicaActionCenter *action_center;
class PicaSysTray *systray;

// /usr/share/pica-client/pica-pica_CA.pem
//QString config_defaultCA("/home/root_jr/files/projects/picapica_wc/localhost/project_picapica/trunk/tests/trusted_CA.pem");
#ifndef WIN32
QString config_defaultCA(PICA_INSTALLPREFIX"/share/pica-client/CA.pem");
#else
QString config_defaultCA("share\\CA.pem");
#endif

static bool create_config_dir()
{
    QMessageBox msgBox;
    config_dir=QDir::homePath()+QDir::separator() +QString(PICA_CLIENT_CONFIGDIR);

    if (!QFile::exists(config_dir))
    {
        msgBox.setText(QString("%1 does not exist").arg(config_dir));//debug
        msgBox.exec();

        QDir dir;

        if (!dir.mkpath(config_dir))
        {
            msgBox.setText(QString(QObject::tr("Unable to create configuration directory:\n %1")).arg(config_dir));
            msgBox.exec();
            return false;
        }
    }

    config_dbname=config_dir+QDir::separator()+QString(PICA_CLIENT_STORAGEDB);

    if (!QFile::exists(config_dbname))
    {
        QSqlDatabase dbconn=QSqlDatabase::addDatabase("QSQLITE");
        dbconn.setDatabaseName(config_dbname);

        if (!dbconn.open())
        {

            msgBox.setText(dbconn.lastError().text());
            msgBox.exec();
            return false;
        }

        QSqlQuery query;

        query.exec("PRAGMA foreign_keys=ON;");

        if (query.lastError().isValid())
                goto showerror;

        query.exec("create table accounts \
                   (\
                       id int primary key, \
                       name varchar(64), \
                       cert_file varchar(255) not null, \
                       pkey_file varchar(255) not null, \
                       ca_file varchar(255) default null \
                       );");

        if (query.lastError().isValid())
                        goto showerror;

        query.exec("create table contacts \
                   (\
                       id int,\
                       name varchar(64), \
                       cert_pem text(2048), \
                       account_id int not null, \
                       primary key(id, account_id),\
                       foreign key(account_id) references accounts(id) on delete cascade\
                    );");

        if (query.lastError().isValid())
                goto showerror;

        query.exec(
                    "create table nodes \
                    (\
                        address varchar(255) not null,\
                        port int not null,\
                        last_active int not null,\
                        inactive_count int not null,\
                        constraint pk primary key (address,port) on conflict replace \
                     );");

         query.exec("insert into nodes values(\"picapica.im\", 2299, 0, 0);");
         query.exec("insert into nodes values(\"picapica.ge\", 2299, 0, 0);");

         if (query.lastError().isValid())
                 goto showerror;

         query.exec("create table history \
                    (\
                        id integer primary key autoincrement, \
                        contact_id int not null, \
                        account_id int not null, \
                        timestamp int not null, \
                        is_me int not null, \
                        is_delivered int not null, \
                        message text(65536) not null, \
                        foreign key(contact_id) references contacts(id) on delete cascade\
                        foreign key(account_id) references accounts(id) on delete cascade\
                    );"
                     );

        if (query.lastError().isValid())
        showerror:
        {
            msgBox.setText(query.lastError().text());
            msgBox.exec();
            return false;
        }

    }


    return true;


}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    if (!create_config_dir())
        return -1;
#ifndef WIN32
    picapica_ico_sit = QIcon(PICA_INSTALLPREFIX"/share/pica-client/picapica-icon-sit.png");
    picapica_ico_fly = QIcon(PICA_INSTALLPREFIX"/share/pica-client/picapica-icon-fly.png");
#else
	picapica_ico_sit = QIcon("share\\picapica-icon-sit.png");
    picapica_ico_fly = QIcon("share\\picapica-icon-fly.png");
#endif

    PicaActionCenter ac;
    action_center = &ac;

    AccountsWindow aw;

    SkyNet s;
    skynet=&s;

    PicaSysTray pst;
    systray = &pst;

    AskPassword askpsw;
    askpassword=&askpsw;

    MsgUIRouter muir;
    msguirouter = &muir;


    aw.show();

    a.setQuitOnLastWindowClosed(false);

    return a.exec();
}
