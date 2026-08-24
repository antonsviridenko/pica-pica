/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
//#include <QtGui/QApplication>
#include <QCoreApplication>
#include <QApplication>
#include <QThread>
#include "mainwindow.h"
#include "accountswindow.h"
#include "globals.h"
#include "contacts.h"
#include "skynet.h"
#include "msguirouter.h"
#include "filetransfercontroller.h"
#include "audiovideocallcontroller.h"
#include "picaactioncenter.h"
#include "picasystray.h"
#include "vaapi.h"
#include <QDir>
#include <QMessageBox>
#include <QString>
#include <QIcon>
#include "../PICA_client.h"
#include "dhparam.h"
#include "settings.h"
#include "dialogs/settingsdialog.h"
#include "audiodevice.h"


//globals
QString config_dir;
QString config_dbname;
QString config_resourceDir;
SkyNet *skynet;
MainWindow *mainwindow;
class AskPassword *askpassword;
class MsgUIRouter *msguirouter;

QIcon picapica_ico_sit;
QIcon picapica_ico_fly;

class PicaActionCenter *action_center;
class PicaSysTray *systray;
class FileTransferController *ftctrl;
class AudioVideoCallController *avctrl;
class AccountsWindow *accwindow;

QString config_defaultDHParam;
QString snd_newmessage;


static bool create_database()
{
	QMessageBox msgBox;
	QSqlDatabase dbconn = QSqlDatabase::database();

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
                   id blob primary key, \
                   name varchar(64), \
                   cert_file varchar(255) not null, \
                   pkey_file varchar(255) not null, \
                   ca_file varchar(255) default null \
                   );");

	if (query.lastError().isValid())
		goto showerror;

	query.exec("create table contacts \
               (\
                   id blob,\
                   name varchar(64), \
                   cert_pem text(2048), \
                   account_id blob not null, \
                   type int not null default 1, \
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
	query.exec("insert into nodes values(\"picapica.im\", 2233, 0, 0);");
	query.exec("insert into nodes values(\"picapica.im\", 2299, 0, 0);");
	query.exec("insert into nodes values(\"picapica.ge\", 2299, 0, 0);");

	if (query.lastError().isValid())
		goto showerror;

	query.exec("create table history \
                (\
                    id integer primary key autoincrement, \
                    contact_id blob not null, \
                    account_id blob not null, \
                    timestamp int not null, \
                    is_me int not null, \
                    is_delivered int not null, \
                    message text(65536) not null, \
                    foreign key(account_id) references accounts(id) on delete cascade\
                );"
	          );

	if (query.lastError().isValid())
		goto showerror;

	query.exec("create table schema_version \
                  ( \
                    ver integer primary key, \
                    timestamp int not null \
                  );"
	          );

	if (query.lastError().isValid())
		goto showerror;

	query.exec("create table settings \
					( \
						name varchar(255) not null, \
                        value varchar(255) not null, \
                        constraint pk primary key (name) on conflict replace \
					);"
	          );

	if (query.lastError().isValid())
		goto showerror;

	query.exec("insert into schema_version values (3, strftime('%s','now'));"); //current schema version. update when needed

	if (query.lastError().isValid())
showerror:
	{
		msgBox.setText(query.lastError().text());
		msgBox.exec();
		return false;
	}

	return true;
}

static bool update_database()
{
	QMessageBox msgBox;
	int schema_ver;

	QSqlDatabase dbconn = QSqlDatabase::database();

	if (!dbconn.open())
	{

		msgBox.setText(dbconn.lastError().text());
		msgBox.exec();
		return false;
	}

	QSqlQuery query;

	//checking if table schema_version exists. If not, then database belongs to pica-client before 0.5.3 version
	query.exec("select count(*) from sqlite_master where name=\"schema_version\"");
	query.next();

	if (query.lastError().isValid())
		goto showerror;

	if (query.value(0).toInt() == 0)
	{
		//--------updating from pre0.5.3 pica-client database
		msgBox.setText("Updating database to newer version");
		msgBox.exec();

		query.exec("create table history \
                   (\
                       id integer primary key autoincrement, \
                       contact_id int not null, \
                       account_id int not null, \
                       timestamp int not null, \
                       is_me int not null, \
                       is_delivered int not null, \
                       message text(65536) not null, \
                       foreign key(account_id) references accounts(id) on delete cascade\
                   );"
		          );

		if (query.lastError().isValid())
			goto showerror;

		query.exec("\
                   create table schema_version \
                     ( \
                       ver integer primary key, \
                       timestamp int not null \
                     );"
		          );

		if (query.lastError().isValid())
			goto showerror;

		query.exec("insert into schema_version values (1, strftime('%s','now'));");
		//--------------------------------------------------
		schema_ver = 1;
	}
	else
	{
		query.exec("select max(ver) from schema_version");
		query.next();

		if (query.lastError().isValid())
			goto showerror;

		schema_ver = query.value(0).toInt();
	}

	if (schema_ver == 1)
	{
		query.exec("PRAGMA foreign_keys=OFF;");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("alter table accounts rename to accounts_old_schemav1;");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("alter table contacts rename to contacts_old_schemav1;");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("alter table history rename to history_old_schemav1;");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("create table accounts \
               (\
                   id blob primary key, \
                   name varchar(64), \
                   cert_file varchar(255) not null, \
                   pkey_file varchar(255) not null, \
                   ca_file varchar(255) default null \
                   );"
		          );

		if (query.lastError().isValid())
			goto showerror;

		query.exec("create table contacts \
                     (\
                      id blob,\
                      name varchar(64), \
                      cert_pem text(2048), \
                      account_id blob not null, \
                      type int not null default 1, \
                      primary key(id, account_id),\
                      foreign key(account_id) references accounts(id) on delete cascade\
                      );"
		          );

		if (query.lastError().isValid())
			goto showerror;

		query.exec("create table history \
                (\
                    id integer primary key autoincrement, \
                    contact_id blob not null, \
                    account_id blob not null, \
                    timestamp int not null, \
                    is_me int not null, \
                    is_delivered int not null, \
                    message text(65536) not null, \
                    foreign key(account_id) references accounts(id) on delete cascade\
                );"
		          );
		if (query.lastError().isValid())
			goto showerror;

		query.exec("alter table accounts_old_schemav1 add column new_id blob;");
		query.exec("alter table contacts_old_schemav1 add column new_id blob;");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("select id, cert_file from accounts_old_schemav1");
		if (query.lastError().isValid())
			goto showerror;

		while(query.next())
		{
			quint32 old_id;
			QString cert_file;
			QSqlQuery q;
			QByteArray new_id(PICA_ID_SIZE, 0);

			old_id = query.value(0).toUInt();
			cert_file = query.value(1).toString();

			if (PICA_get_id_from_cert_file(cert_file.toUtf8().constData(), (unsigned char*)new_id.data()))
			{
				QSqlQuery q;

				q.prepare("update accounts_old_schemav1 set new_id = :new_id where id = :id");
				q.bindValue(":new_id", new_id);
				q.bindValue(":id", old_id);
				q.exec();
			}
		}

		query.exec("select id, cert_pem from contacts_old_schemav1");
		if (query.lastError().isValid())
			goto showerror;

		while(query.next())
		{
			quint32 old_id;
			QString cert_pem;
			QSqlQuery q;
			QByteArray new_id(PICA_ID_SIZE, 0);

			old_id = query.value(0).toUInt();
			cert_pem = query.value(1).toString();

			if (PICA_get_id_from_cert_string(cert_pem.toUtf8().constData(), (unsigned char *)new_id.data()))
			{
				QSqlQuery q;

				q.prepare("update contacts_old_schemav1 set new_id = :new_id where id = :id");
				q.bindValue(":new_id", new_id);
				q.bindValue(":id", old_id);
				q.exec();
			}
		}


		query.exec("insert into accounts select new_id, name, cert_file, pkey_file, ca_file from accounts_old_schemav1 where new_id is not null");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("insert into contacts select c.new_id, c.name, c.cert_pem, a.new_id, 1 from accounts_old_schemav1 as a, contacts_old_schemav1 as c on c.account_id = a.id where c.new_id is not null and a.new_id is not null");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("insert into history select h.id, c.new_id, a.new_id, h.timestamp, h.is_me, h.is_delivered, h.message \
                    from history_old_schemav1 as h, accounts_old_schemav1 as a, contacts_old_schemav1 as c on h.contact_id = c.id and h.account_id = a.id and a.id = c.account_id where c.new_id is not null and a.new_id is not null order by h.id");

		if (query.lastError().isValid())
			goto showerror;

		query.exec("insert into schema_version values (2, strftime('%s','now'));");
		schema_ver = 2;
	}

	if (schema_ver == 2)
	{
		query.exec("create table settings \
					( \
						name varchar(255) not null, \
                        value varchar(255) not null, \
                        constraint pk primary key (name) on conflict replace \
					);"
		          );

		if (query.lastError().isValid())
			goto showerror;

		query.exec("insert into schema_version values (3, strftime('%s','now'));");
		schema_ver = 3;
	}

	if (query.lastError().isValid())
showerror:
	{
		msgBox.setText(query.lastError().text() + " " + query.lastQuery());
		msgBox.exec();
		return false;
	}

	return true;
}

static bool create_config_dir()
{
	QMessageBox msgBox;
	config_dir = QDir::homePath() + QDir::separator() + QString(PICA_CLIENT_CONFIGDIR);

	if (!QFile::exists(config_dir))
	{
		QDir dir;

		if (!dir.mkpath(config_dir))
		{
			msgBox.setText(QString(QObject::tr("Unable to create configuration directory:\n %1")).arg(config_dir));
			msgBox.exec();
			return false;
		}
	}
	QFile::setPermissions(config_dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

	config_dbname = config_dir + QDir::separator() + QString(PICA_CLIENT_STORAGEDB);

	QSqlDatabase dbconn = QSqlDatabase::addDatabase("QSQLITE");
	dbconn.setDatabaseName(config_dbname);

	if (!QFile::exists(config_dbname))
	{
		if (!create_database())
			return false;
	}
	else
		update_database();

	QFile::setPermissions(config_dbname, QFile::ReadOwner | QFile::WriteOwner);

	// Publish the audio driver choice to the audio threads while we are still
	// the only thread and the settings database is reachable - see
	// AudioDevice::SetLinuxDriverName() for why it cannot be read on demand.
	{
		Settings audiost(config_dbname);
		AudioDevice::SetLinuxDriverName(audiost.loadValue("audio.driver", "alsa").toString());
	}

	/*if (!QFile::exists(config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE))
	{
	    OpenSSLTool osslt;
	    msgBox.setText(QString(QObject::tr("Diffie-Hellman parameters will be generated. Please wait. This process can take several minutes...")));
	    msgBox.exec();

	    if (!osslt.GenDHParam(4096, config_dir + QDir::separator() + PICA_CLIENT_DHPARAMFILE))
	    {
	        msgBox.setText(QString(QObject::tr("Failed to generate Diffie-Hellman parameters")));
	        msgBox.exec();
	        return false;
	    }
	}*/
	/*
	QString dh_error_message;

	if (!DHParam::VerifyGenerated(NULL))
	{
	    QMessageBox dhmsgBox;
	    dhmsgBox.setText(QObject::tr("Do you want to generate new Diffie-Hellman parameter file?"));
	    dhmsgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	    dhmsgBox.setDefaultButton(QMessageBox::No);
	    dhmsgBox.setInformativeText(QObject::tr("If you press \"Yes\", new Diffie-Hellman parameter generation will be started. It is a CPU intensive process and can take undetermined amount of time (1 - 20 minutes on modern computers). Default DH parameter file will be used for starting secure communications until the new DH parameter generation is finished.\nIf you press \"No\", default DH parameter file distributed with Pica Pica Messenger will be used for every communication session."));
	    switch (dhmsgBox.exec())
	    {
	    case QMessageBox::No:

	    DHParam::UseDefault();

	    break;

	    case QMessageBox::Yes:

	    DHParam::StartNewDHParamGeneration(&dh_error_message);

	    break;
	    }

	    if (!DHParam::VerifyDefault(&dh_error_message))
	    {
	        msgBox.setText(dh_error_message);
	        msgBox.setIcon(QMessageBox::Warning);
	        msgBox.setStandardButtons(QMessageBox::Ok);
	        msgBox.exec();
	    }
	}*/

	return true;
}

int main(int argc, char *argv[])
{
#ifdef HAVE_VAAPI
	// Before Qt opens its X connection, which is the only time Xlib will
	// listen - see the comment on the function itself.
	VaapiContext::initX11Threading();
#endif

	QApplication a(argc, argv);

#ifdef HAVE_VAAPI
	// Decoded hardware frames travel from the decoding thread to the widget
	// drawing them through queued signals, which needs their type registered.
	VaapiContext::registerMetaTypes();

	// Settles how a decoded surface will be drawn in this session, which has
	// to be known before the first one is created.
	VaapiContext::detectX11Display();
#endif

#ifdef WIN32
	config_resourceDir = QLatin1String("share\\");
#elif defined(__APPLE__)
	config_resourceDir = QCoreApplication::applicationDirPath() + QLatin1String("/../Resources/");
#else
	config_resourceDir = QLatin1String(PICA_INSTALLPREFIX "/share/pica-client/");
#endif

	if (!create_config_dir())
		return -1;

	config_defaultDHParam = config_resourceDir + QLatin1String(PICA_CLIENT_DHPARAMFILE);
	snd_newmessage = config_resourceDir + QLatin1String("picapica-snd-newmessage.wav");

	picapica_ico_sit = QIcon(config_resourceDir + "picapica-icon-sit.png");
	picapica_ico_fly = QIcon(config_resourceDir + "picapica-icon-fly.png");

	Settings st(config_dbname);

	if (st.isEmpty()) //show settings dialog on first start
	{
		SettingsDialog sd;

		sd.exec();
	}

	PicaActionCenter ac;
	action_center = &ac;

	AccountsWindow aw;
	accwindow = &aw;

	SkyNet s;
	skynet = &s;

	PicaSysTray pst;
	systray = &pst;

	AskPassword askpsw;
	askpassword = &askpsw;

	MsgUIRouter muir;
	msguirouter = &muir;

	FileTransferController ftc;
	ftctrl = &ftc;

	AudioVideoCallController avc;
	avctrl = &avc;

	// SkyNet owns all the protocol/network state (PICA_c2n/PICA_c2c
	// connections, the timer-driven PICA_event_loop() polling, and every
	// protocol callback dispatch). It's moved off the GUI thread onto its
	// own network thread so that GUI-thread stalls (blocked X11/graphics
	// calls, a locked screen suspending the process, etc.) can no longer
	// starve the network connection and cause disconnects. Its public
	// methods remain safe to call from any thread - see SkyNet::init() and
	// the do_*() wrapper pattern in skynet.cpp/h.
	//
	// Done last, after every controller above has already connected its
	// signal/slot listeners to skynet while it was still on the GUI
	// thread, so there is no window during which a listener could miss a
	// signal skynet emits from its new thread.
	QThread networkThread;
	networkThread.setObjectName("SkyNetThread");

	skynet->moveToThread(&networkThread);
	QObject::connect(&networkThread, SIGNAL(started()), skynet, SLOT(init()));
	networkThread.start();

	aw.show();

	a.setQuitOnLastWindowClosed(false);

	int ret = a.exec();

	// skynet's thread affinity needs to move back to the main thread
	// before it (and everything else declared above) gets destroyed as
	// main() returns - otherwise QObject's destructor can't clean up its
	// still-registered timers (event_loop_timer_id, c2c_reconnect_timer_id,
	// ...) and prints "Timers cannot be stopped from another thread".
	//
	// QObject::moveToThread() only succeeds when called from the thread
	// the object is currently on - it can be pushed out from its own
	// thread, but not pulled in from a different one. So this can't be a
	// plain skynet->moveToThread(...) call made from here (the main
	// thread); it has to run as code executing on the network thread
	// itself, which is what moveBackToMainThread() is for. Using a
	// blocking queued call guarantees it has actually completed before
	// the network thread is stopped below - if it ran after quit()/wait(),
	// there would be no event loop left on that thread to process it.
	QMetaObject::invokeMethod(skynet, "moveBackToMainThread", Qt::BlockingQueuedConnection);

	// Now stop and join the network thread before any of the objects
	// above - skynet in particular - start being destroyed as main()
	// unwinds. Otherwise the network thread could still be executing
	// SkyNet code while its destructor (and the destructors of objects it
	// signals) runs concurrently on this thread.
	networkThread.quit();
	networkThread.wait();

	return ret;
}
