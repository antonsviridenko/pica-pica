#ifndef GLOBALS_H
#define GLOBALS_H

#define PICA_CLIENT_STORAGEDB "pica-client.sqlite"
#define PICA_CLIENT_CONFIGDIR ".pica-client"

#ifndef PICA_INSTALLPREFIX

#define PICA_INSTALLPREFIX "/usr/"

#endif

//variables are defined in main.cpp
extern class QString config_dir;
extern class QString config_dbname;
extern class QString config_defaultCA;
extern class SkyNet *skynet;
extern class MainWindow *mainwindow;
extern unsigned int account_id;
extern class AskPassword *askpassword;
extern class MsgUIRouter *msguirouter;
extern class QIcon picapica_ico_sit;
extern class QIcon picapica_ico_fly;
extern class PicaActionCenter *action_center;
extern class PicaSysTray *systray;
#endif // GLOBALS_H
