/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
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
extern class QString config_defaultDHParam;
extern class QString config_resourceDir;
extern class SkyNet *skynet;
extern class MainWindow *mainwindow;
extern class AskPassword *askpassword;
extern class MsgUIRouter *msguirouter;
extern class QIcon picapica_ico_sit;
extern class QIcon picapica_ico_fly;
extern class PicaActionCenter *action_center;
extern class PicaSysTray *systray;
extern class FileTransferController *ftctrl;
extern class QString snd_newmessage;
extern class AccountsWindow *accwindow;
#endif // GLOBALS_H
