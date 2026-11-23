/*
	(c) Copyright  2012 - 2026 Anton Sviridenko
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
#include "vaapix11widget.h"

#ifdef HAVE_VAAPI_X11

#include <QDebug>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>

// Included after the Qt headers, since Xlib defines a handful of names as
// macros that Qt also uses as ordinary identifiers.
#include <X11/Xlib.h>
#include <va/va_x11.h>
#undef Bool
#undef Status
#undef None
#undef Success
#undef Always
#undef CursorShape
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef Expose
#undef Unsorted

VaapiX11Widget::VaapiX11Widget(QWidget *parent)
	: QWidget(parent), m_working(true), m_reportedFailure(false),
	  m_describedSurface(false), m_everDrew(false), m_failuresInARow(0)
{
	// The driver draws onto this window itself, which means it has to be a
	// window of its own rather than a region of its parent's, and Qt has to
	// keep out of it: no background filling, no double buffering, no clearing
	// before a paint event.
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAutoFillBackground(false);
}

VaapiX11Widget::~VaapiX11Widget()
{
}

void VaapiX11Widget::reportFailure(const QString &message)
{
	m_working = false;
	markRenderingBroken();

	if (m_reportedFailure)
		return;

	m_reportedFailure = true;
	qWarning() << message;
	emit renderingFailed(message);
}

void VaapiX11Widget::setFrame(AVFramePtr frame)
{
	if (!m_working)
		return;

	m_frame = frame;

	// repaint() rather than update(): the frame is only held until the next
	// one replaces it, and a posted paint event could easily be delivered
	// after that has happened.
	repaint();
}

void VaapiX11Widget::clearFrame()
{
	m_frame.clear();
	clearWindow();
}

void VaapiX11Widget::clearWindow()
{
	Display *dpy = (Display *)VaapiContext::x11Display();

	if (!dpy || !isVisible())
		return;

	XClearWindow(dpy, (Window)winId());
	XFlush(dpy);
}

void VaapiX11Widget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);

	Display *dpy = (Display *)VaapiContext::x11Display();

	if (!dpy)
		return;

	// Black, so that the bars beside a letterboxed picture, and the window
	// before the first frame arrives, are not left showing whatever the
	// server last had there.
	XSetWindowBackground(dpy, (Window)winId(), BlackPixel(dpy, DefaultScreen(dpy)));
	XClearWindow(dpy, (Window)winId());
	XFlush(dpy);
}

void VaapiX11Widget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	// The picture is about to be drawn at a different size, and what the old
	// one left outside it would otherwise stay on screen.
	clearWindow();
}

void VaapiX11Widget::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);

	if (!m_working || !m_frame)
		return;

	// Nothing to draw onto until the window is on screen, and asking the
	// driver to draw on one that is not would fail for a reason that says
	// nothing about whether this path works.
	if (!isVisible())
		return;

	if (putFrame())
	{
		m_everDrew = true;
		m_failuresInARow = 0;
		return;
	}

	m_failuresInARow++;

	// Frames keep arriving, so one failure proves nothing - give the window
	// time to settle before writing the path off. Once something has been
	// drawn successfully, a later failure is treated the same way: only a
	// sustained run of them counts.
	if (m_failuresInARow < kMaxFailuresInARow)
		return;

	reportFailure(m_everDrew
	              ? tr("Stopped being able to put decoded GPU frames on screen")
	              : tr("Could not put the decoded GPU frame on screen"));
}

bool VaapiX11Widget::putFrame()
{
	VADisplay va_display = VaapiContext::display();

	if (!va_display || !m_frame)
		return false;

	int frameWidth = m_frame->width;
	int frameHeight = m_frame->height;

	if (frameWidth <= 0 || frameHeight <= 0)
		return false;

	// Letterbox rather than stretch, matching what the software path does with
	// Qt::KeepAspectRatio. Everything the picture does not cover keeps the
	// window's black background.
	int windowWidth = width();
	int windowHeight = height();
	int destWidth = windowWidth;
	int destHeight = (int)((qint64)windowWidth * frameHeight / frameWidth);

	if (destHeight > windowHeight)
	{
		destHeight = windowHeight;
		destWidth = (int)((qint64)windowHeight * frameWidth / frameHeight);
	}

	int destX = (windowWidth - destWidth) / 2;
	int destY = (windowHeight - destHeight) / 2;

	// The surface id lives in data[3] for AV_PIX_FMT_VAAPI frames.
	VASurfaceID surface = (VASurfaceID)(uintptr_t)m_frame->data[3];

	Display *dpy = (Display *)VaapiContext::x11Display();

	// Before the first frame goes out, make sure the server has caught up with
	// Qt's own requests for this window - it is created and mapped on the same
	// connection, but not necessarily processed by the time a frame arrives,
	// and the driver cannot draw on a window the server does not have yet.
	if (dpy && !m_everDrew)
		XSync(dpy, False);

	if (!m_describedSurface)
	{
		m_describedSurface = true;
		qDebug() << QString("Putting VAAPI surfaces on an X11 window: %1x%2")
		            .arg(frameWidth).arg(frameHeight);
	}

	// VA_SRC_BT601 to match the coefficients the software path and the EGL
	// renderer use for camera video; VA_FRAME_PICTURE because nothing in this
	// pipeline is interlaced.
	VAStatus st = vaPutSurface(va_display, surface, (Drawable)winId(),
	                           0, 0, frameWidth, frameHeight,
	                           destX, destY, destWidth, destHeight,
	                           nullptr, 0,
	                           VA_FRAME_PICTURE | VA_SRC_BT601);

	if (st != VA_STATUS_SUCCESS)
	{
		// Only the first one, and then only every so often: at video rate a
		// line per frame would bury everything else in the log.
		if (m_failuresInARow == 0 || m_failuresInARow == kMaxFailuresInARow - 1)
			qWarning() << QString("vaPutSurface failed: %1 (0x%2)")
			              .arg(QLatin1String(vaErrorStr(st))).arg((uint)st, 0, 16);

		return false;
	}

	return true;
}

#endif // HAVE_VAAPI_X11
