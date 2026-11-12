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
#include "dialogs/nodesdialog.h"

#include <QMessageBox>
#include <QLabel>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QtMath>

// easter egg: clicking the icon in the About box releases a flock of picas over the whole screen.
// One see-through layer covers the screen and the birds fly inside it.
class PicaSky : public QWidget
{
public:
	PicaSky(QWidget *box, QPoint click) :
		QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
		launched_(false)
	{
		QScreen *screen = box->windowHandle() ? box->windowHandle()->screen() : QGuiApplication::primaryScreen();
		QRect area = screen->geometry();

		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_ShowWithoutActivating);
		setAttribute(Qt::WA_DeleteOnClose);
		setGeometry(area);

		if (QGuiApplication::platformName() != "wayland")
		{
			show();
			launch(click - area.topLeft());
			return;
		}

		// Wayland hides where the dialog is, but tells a new surface where the pointer is when it
		// appears under it - that is enterEvent(). Until then the layer has to accept input.
		showFullScreen(); // the only way Wayland lets a window cover the screen

		// no pointer report (e.g. the pointer left the screen): guess the compositor centered the dialog
		QPoint guess = QPoint((area.width() - box->width()) / 2, (area.height() - box->height()) / 2) +
		               box->mapFromGlobal(click);
		QTimer::singleShot(300, this, [this, guess] { launch(guess); });
	}

protected:
	void enterEvent(QEvent *e) override
	{
		launch(static_cast<QEnterEvent *>(e)->pos());
	}

private:
	bool launched_;

	void launch(QPoint start)
	{
		if (launched_)
			return;
		launched_ = true;

		// from here on clicks pass through to whatever is under the birds
		windowHandle()->setFlags(windowHandle()->flags() | Qt::WindowTransparentForInput);

		QRandomGenerator *rnd = QRandomGenerator::global();
		const int size = 32;
		const int distance = width() + height();
		int longest = 0;

		for (int i = 0; i < 7; i++)
		{
			QLabel *bird = new QLabel(this);
			bird->setPixmap(picapica_ico_fly.pixmap(size));
			bird->resize(size, size);
			bird->move(start - QPoint(size / 2, size / 2));
			bird->show();

			// mostly upwards, fanned out to both sides
			qreal angle = qDegreesToRadians(-160.0 + rnd->bounded(140.0));
			QPoint to = bird->pos() + QPoint(qCos(angle) * distance, qSin(angle) * distance);

			QPropertyAnimation *fly = new QPropertyAnimation(bird, "pos", bird);
			fly->setDuration(1500 + rnd->bounded(1000));
			fly->setEasingCurve(QEasingCurve::InQuad); // slow take-off, then away
			fly->setEndValue(to);
			fly->start();
			longest = qMax(longest, fly->duration());
		}

		QTimer::singleShot(longest + 100, this, &QWidget::close);
	}
};

class PicaIconClick : public QObject
{
public:
	bool eventFilter(QObject *o, QEvent *e) override
	{
		if (e->type() == QEvent::MouseButtonPress)
			new PicaSky(static_cast<QWidget *>(o)->window(), static_cast<QMouseEvent *>(e)->globalPos());

		return false;
	}
};

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

	nodesAct = new QAction(tr("Show &Nodes..."), this);
	connect(nodesAct, SIGNAL(triggered()), this, SLOT(shownodes()));
}

void PicaActionCenter::about()
{
#ifdef PACKAGE_VERSION
#define VERSION_STRING "v" PACKAGE_VERSION
#else
#define VERSION_STRING ""
#endif
	QMessageBox mbx;
	mbx.setWindowIcon(picapica_ico_sit);
	mbx.setIconPixmap(picapica_ico_sit.pixmap(32));
	mbx.setTextFormat(Qt::RichText);
	mbx.setText(tr("<b>Pica Pica Messenger " VERSION_STRING "<br>Copyright (c) 2012 - 2019 Anton Sviridenko</b><br><br>\
<a href=https://picapica.im>https://picapica.im</a><br><br>\
<a href=\"https://picapica.im/?page_id=192&lang=en\">User Manual</a><br><br>\
protocol version " PICA_PROTO_VER_STRING "<br>"
"client protocol version " PICA_PROTO_CLIENT_VER_STRING "<br>"
"<pre>"
"This program is free software: you can redistribute it and/or modify" "<br>"
"it under the terms of the GNU General Public License as published by" "<br>"
"the Free Software Foundation, version 3." "<br>"
"<br><br>"
"This program is distributed in the hope that it will be useful," "<br>"
"but WITHOUT ANY WARRANTY; without even the implied warranty of" "<br>"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the" "<br>"
"GNU General Public License for more details." "<br>"
"<br><br>"
"You should have received a copy of the GNU General Public License" "<br>"
"along with this program.  If not, see &lt;https://www.gnu.org/licenses/&gt;.</pre><br>"));
	// clicking the icon releases the picas
	PicaIconClick iconclick;
	QLabel *icon = mbx.findChild<QLabel *>("qt_msgboxex_icon_label");
	if (icon)
		icon->installEventFilter(&iconclick);

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

void PicaActionCenter::shownodes()
{
	NodesDialog nd;
	nd.exec();
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
