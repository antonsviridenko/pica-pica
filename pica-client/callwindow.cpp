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
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

// Smallest the video area is allowed to become. Low enough that the window
// stays freely resizable - the video area is the part that gives, so a floor
// here is a floor under the whole window - while still leaving something
// recognizable on screen.
static const QSize kMinVideoSize(160, 120);

CallWindow::CallWindow(QByteArray peer_id, bool incoming)
	: m_peer_id(peer_id), is_incoming(incoming), videoAreaOpened(false)
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
	lbTransport = new QLabel(this);
	lbTransport->setAlignment(Qt::AlignCenter);
	lbTransport->hide();
	lbVideo = new QLabel(this);
	lbVideo->setAlignment(Qt::AlignCenter);
	// A label holding a pixmap reports that pixmap's size as both its
	// preferred and its minimum size, which would make the video area's size
	// depend on the picture currently in it: the window could not be made
	// smaller than the last frame drawn, and the layout would be recomputed on
	// every frame. Ignored breaks that - the label is given whatever space the
	// stretch factors below say and never asks for any.
	lbVideo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	// Small enough to let the window be resized freely; the size the video
	// area actually opens at is set once by openVideoArea().
	lbVideo->setMinimumSize(kMinVideoSize);
	lbVideo->hide();
#ifdef HAVE_VAAPI
	videoGpu = VaapiRenderWidget::create(this);
	videoGpu->widget()->setMinimumSize(kMinVideoSize);
	videoGpu->widget()->hide();
	connect(videoGpu->widget(), SIGNAL(renderingFailed(QString)), this, SLOT(gpu_rendering_failed(QString)));
#endif
	callTimer = new QTimer(this);
	callElapsedSeconds = 0;

	// Only the video gets a stretch factor, so it takes all the height the
	// rows around it do not need. Left at the default of zero, every item
	// whose size policy allows it to grow gets an equal share of the spare
	// height instead - which for two QLabels and the video meant the video
	// got about a third of it and the timer and the transport line sat in
	// tall empty bands.
	//
	// Only one of the two video widgets is ever visible and a hidden widget is
	// given no space, so both can be stretched.
	lv->addWidget(lbTimer, 0);
	lv->addWidget(lbTransport, 0);
	lv->addWidget(lbVideo, 1);
#ifdef HAVE_VAAPI
	lv->addWidget(videoGpu->widget(), 1);
#endif

	// Note that the second argument of addWidget() is a stretch factor and not
	// an alignment - passing Qt::AlignLeft here, as this did, compiles, since
	// Qt::Alignment converts to int, and quietly means a stretch of 1. A
	// spacer does what the alignment was meant to.
	lh->addWidget(pbAccept);
	lh->addWidget(pbCall);
	lh->addStretch(1);
	lh->addWidget(pbHang);
	lv->addLayout(lh, 0);
	setLayout(lv);

	lbVideo->installEventFilter(this);

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
	lbTransport->hide();
}

void CallWindow::setMediaTransport(bool direct_udp, QString ciphersuitename)
{
	if (direct_udp)
		lbTransport->setText(tr("🔐 media: direct UDP, %1").arg(ciphersuitename));
	else
		lbTransport->setText(tr("media: through the chat connection"));

	lbTransport->show();
}

void CallWindow::showRemoteFrame(QImage frame)
{
	if (frame.isNull())
		return;

	lastRemoteFrame = frame;

	if (lbVideo->isHidden())
	{
		openVideoArea(frame.size());
		lbVideo->show();
		reportVideoArea();
	}

	updateRemoteFrame();
}

void CallWindow::updateRemoteFrame()
{
	if (lastRemoteFrame.isNull() || lbVideo->isHidden())
		return;

	// Normally a no-op: the decoder was told the size of this area and the
	// frame already arrived at it, and scale() returns the frame untouched
	// when there is nothing to do. It earns its place across a resize, when
	// the frames already decoded at the old size have still to be drawn at
	// the new one.
	QImage scaled = videoScaler.scale(lastRemoteFrame, lbVideo->size());

	if (scaled.isNull())
		return;

	lbVideo->setPixmap(QPixmap::fromImage(scaled));
}

void CallWindow::reportVideoArea()
{
	// While the label is hidden there is nothing to size frames for, and its
	// geometry is meaningless anyway. isHidden() rather than isVisible(): what
	// matters is whether this label is the renderer in use, not whether the
	// window happens to be on screen right now. When the GPU widget has the
	// video area it scales frames itself, on the GPU, and wants them at their
	// own resolution - see showRemoteHwFrame().
	if (lbVideo->isHidden())
		return;

	emit video_area_changed(lbVideo->size());
}

void CallWindow::openVideoArea(QSize frame)
{
	if (videoAreaOpened || frame.isEmpty())
		return;

	videoAreaOpened = true;

	// The window has been sized for a call with no video in it, so the video
	// area is about to appear into whatever the layout can spare - which is
	// its minimum. Grow the window by what the video needs on top of that.
	//
	// Only the first frame does this. After it the window's size is the user's
	// business, and a later frame must not undo a deliberate resize.
	QSize wanted = frame;
	QScreen *screen = QGuiApplication::primaryScreen();

	if (screen)
	{
		// Leave the rows above and below the video the height they have
		// now - a peer sending 1080p to a 1080 high screen should not open
		// a window taller than the desktop.
		QSize available = screen->availableGeometry().size() - QSize(0, height());

		wanted = VideoScaler::fitted(frame, available.expandedTo(kMinVideoSize));
	}

	resize(qMax(width(), wanted.width()),
	       height() + qMax(0, wanted.height() - lbVideo->height()));
}

#ifdef HAVE_VAAPI
void CallWindow::showRemoteHwFrame(AVFramePtr frame)
{
	if (!frame)
		return;

	// The widget has given up on drawing - ask again for frames the label can
	// show, and keep the video area with it.
	if (!videoGpu->isWorking())
	{
		emit video_rendering_failed();
		return;
	}

	// These frames hold a GPU surface rather than pixels, so the label cannot
	// show them - the GL widget takes over the video area instead.
	if (videoGpu->widget()->isHidden())
	{
		openVideoArea(QSize(frame->width, frame->height));
		lbVideo->hide();
		videoGpu->widget()->show();

		// The widget scales the surface itself, on the GPU, so the decoder
		// has no reason to produce frames at any particular size - and it
		// is not producing these ones at all.
		emit video_area_changed(QSize());
	}

	videoGpu->setFrame(frame);
}

void CallWindow::gpu_rendering_failed(QString message)
{
	// Nothing will ever appear in the GL widget now, so hand the video area
	// back to the label and ask for frames it can actually show. Without this
	// the rest of the call would be spent looking at an empty black rectangle.
	qWarning() << message;

	videoGpu->widget()->hide();
	lbVideo->show();

	// Frames are about to start arriving in system memory, so the decoder
	// needs the size to produce them at. Sent explicitly rather than left to
	// the label's resize, which does not fire if showing it did not change its
	// geometry.
	reportVideoArea();

	emit video_rendering_failed();
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

bool CallWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == lbVideo && event->type() == QEvent::Resize)
	{
		reportVideoArea();
		updateRemoteFrame();
	}

	return QWidget::eventFilter(watched, event);
}

void CallWindow::closeEvent(QCloseEvent *e)
{
	emit callwindow_closed(this);

	e->accept();
}
