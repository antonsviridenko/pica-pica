#ifndef GLOBALS_H
#define GLOBALS_H

#define PICA_CLIENT_STORAGEDB "pica-client.sqlite"
#define PICA_CLIENT_CONFIGDIR ".pica-client"
#define PICA_CLIENT_DHPARAMFILE "dhparam4096.pem"
#define PICA_CLIENT_DHPARAMBITS 4096

#ifndef PICA_INSTALLPREFIX

#define PICA_INSTALLPREFIX "/usr"

#endif

#include "../PICA_id.h"

//variables are defined in main.cpp
extern class QString config_dir;
extern class QString config_dbname;
extern class QString config_defaultCA;
extern class QString config_defaultDHParam;
extern class SkyNet *skynet;
extern class MainWindow *mainwindow;
extern class AskPassword *askpassword;
extern class MsgUIRouter *msguirouter;
extern class QIcon picapica_ico_sit;
extern class QIcon picapica_ico_fly;
extern class PicaActionCenter *action_center;
extern class PicaSysTray *systray;
#endif // GLOBALS_H
