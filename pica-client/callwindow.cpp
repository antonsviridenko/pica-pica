/*
	(c) Copyright  2012 - 2020 Anton Sviridenko
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

#include "callwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCloseEvent>

CallWindow::CallWindow(QByteArray peer_id, bool incoming)
	: m_peer_id(peer_id), is_incoming(incoming)
{
	setAttribute(Qt::WA_DeleteOnClose);

	QVBoxLayout *lv = new QVBoxLayout(this);
	QHBoxLayout *lh = new QHBoxLayout();

	pbAccept = new QPushButton(tr("Accept"), this);
	pbCall = new QPushButton(tr("Call"), this);
	pbHang = new QPushButton(tr("Hang Up"), this);
	lbTimer = new QLabel(this);
	lbTimer->setAlignment(Qt::AlignCenter);
	lbTimer->hide();
	lbVideo = new QLabel(this);
	lbVideo->setAlignment(Qt::AlignCenter);
	lbVideo->setMinimumSize(320, 240);
	lbVideo->hide();
#ifdef HAVE_VAAPI
	videoGpu = new VaapiVideoWidget(this);
	videoGpu->setMinimumSize(320, 240);
	videoGpu->hide();
#endif
	callTimer = new QTimer(this);
	callElapsedSeconds = 0;

	lv->addWidget(lbTimer);
	lv->addWidget(lbVideo);
#ifdef HAVE_VAAPI
	lv->addWidget(videoGpu);
#endif

	lh->addWidget(pbAccept, Qt::AlignLeft);
	lh->addWidget(pbCall, Qt::AlignLeft);
	lh->addWidget(pbHang, Qt::AlignRight);
	lv->addLayout(lh);
	setLayout(lv);

	connect(pbCall, SIGNAL(clicked()), this, SLOT(call()));
	connect(pbHang, SIGNAL(clicked()), this, SLOT(hang()));
	connect(pbAccept, SIGNAL(clicked()), this, SLOT(accept()));
	connect(callTimer, SIGNAL(timeout()), this, SLOT(update_call_timer()));

	if (is_incoming)
	{
		pbCall->hide();
	}
	else
	{
		pbAccept->hide();
	}
}

void CallWindow::call_started()
{
	pbCall->hide();
	pbAccept->hide();

	callElapsedSeconds = 0;
	lbTimer->setText("00:00");
	lbTimer->show();
	callTimer->start(1000);
}

void CallWindow::call_ended()
{
	callTimer->stop();
	pbHang->setDisabled(true);
}

void CallWindow::showRemoteFrame(QImage frame)
{
	if (frame.isNull())
		return;

	if (lbVideo->isHidden())
		lbVideo->show();

	lbVideo->setPixmap(QPixmap::fromImage(frame).scaled(lbVideo->size(),
	                                                    Qt::KeepAspectRatio,
	                                                    Qt::SmoothTransformation));
}

#ifdef HAVE_VAAPI
void CallWindow::showRemoteHwFrame(AVFramePtr frame)
{
	if (!frame)
		return;

	// These frames hold a GPU surface rather than pixels, so the label cannot
	// show them - the GL widget takes over the video area instead.
	if (videoGpu->isHidden())
	{
		lbVideo->hide();
		videoGpu->show();
	}

	videoGpu->setFrame(frame);
}
#endif

void CallWindow::update_call_timer()
{
	int hours, minutes, seconds;
	QString text;

	callElapsedSeconds++;

	hours = callElapsedSeconds / 3600;
	minutes = (callElapsedSeconds % 3600) / 60;
	seconds = callElapsedSeconds % 60;

	if (hours > 0)
		text = QString("%1:%2:%3").
		    arg(hours, 2, 10, QChar('0')).
		    arg(minutes, 2, 10, QChar('0')).
		    arg(seconds, 2, 10, QChar('0'));
	else
		text = QString("%1:%2").
		    arg(minutes, 2, 10, QChar('0')).
		    arg(seconds, 2, 10, QChar('0'));

	lbTimer->setText(text);
}

void CallWindow::call()
{
	emit start_call_pressed();
}

void CallWindow::hang()
{
	emit hang_call_pressed();
}

void CallWindow::accept()
{
	emit accept_call_pressed();
}

void CallWindow::closeEvent(QCloseEvent *e)
{
	emit callwindow_closed(this);

	e->accept();
}
