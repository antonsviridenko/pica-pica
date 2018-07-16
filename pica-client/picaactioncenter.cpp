#include "picaactioncenter.h"
#include "globals.h"
#include "mainwindow.h"
#include "skynet.h"
#include <QApplication>
#include "../PICA_proto.h"
#include "accounts.h"
#include "dialogs/showpicaiddialog.h"
#include "sound.h"
#include "dialogs/settingsdialog.h"

#include <QMessageBox>

PicaActionCenter::PicaActionCenter(QObject *parent) :
	QObject(parent)
{
	exitAct = new QAction(tr("E&xit"), this);
	exitAct->setStatusTip(tr("Close Pica Pica messenger and exit"));
	connect(exitAct, SIGNAL(triggered()), this, SLOT(exit()));

	aboutAct = new QAction(tr("A&bout"), this);
	aboutAct->setStatusTip(tr("About Pica Pica Messenger"));
	connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

	showmyidAct = new QAction(tr("Show My Pica Pica ID"), this);
	connect(showmyidAct, SIGNAL(triggered()), this, SLOT(showmyid()));

	muteSoundsAct = new QAction(tr("&Mute Sounds"), this);
	muteSoundsAct->setCheckable(true);
	connect(muteSoundsAct, SIGNAL(triggered(bool)), this, SLOT(muteSounds(bool)));

	settingsAct = new QAction(tr("&Configure Messenger..."), this);
	connect(settingsAct, SIGNAL(triggered(bool)), this, SLOT(showsettings()));
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
	mbx.setText(tr("<b>Pica Pica Messenger "VERSION_STRING"<br>Copyright (c) 2012 - 2018 Anton Sviridenko</b><br>\
<a href=http://picapica.im>http://picapica.im</a><br><br>Contributors:<br>Daniil Ustinov - bugfixes<br>EXL - bugfixes<br><br>\
protocol version " PICA_PROTO_VER_STRING "<br>"
"client protocol version " PICA_PROTO_CLIENT_VER_STRING "<br><br>"
"<pre>"
"This program is free software: you can redistribute it and/or modify" "<br>"
"it under the terms of the GNU General Public License as published by" "<br>"
"the Free Software Foundation, either version 3 of the License, or" "<br>"
"(at your option) any later version." "<br>"
"<br><br>"
"This program is distributed in the hope that it will be useful," "<br>"
"but WITHOUT ANY WARRANTY; without even the implied warranty of" "<br>"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the" "<br>"
"GNU General Public License for more details." "<br>"
"<br><br>"
"You should have received a copy of the GNU General Public License" "<br>"
"along with this program.  If not, see &lt;https://www.gnu.org/licenses/&gt;.</pre><br>"));
	mbx.exec();
}

void PicaActionCenter::exit()
{
	if (mainwindow)
		mainwindow->close();

	skynet->Exit();
	QApplication::instance()->quit();
}

void PicaActionCenter::showsettings()
{
	SettingsDialog sd;
	sd.exec();
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
