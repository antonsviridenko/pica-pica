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
#ifndef VAAPI_H
#define VAAPI_H

#ifdef HAVE_VAAPI

#include <QSharedPointer>
#include <QMetaType>
#include <va/va.h>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
}

// A decoded hardware frame holds a VA surface, not pixels, and has to travel
// from the decoding thread to the widget that draws it. Reference counting it
// this way keeps the surface alive exactly as long as someone still needs it,
// and lets it cross threads through an ordinary queued signal.
typedef QSharedPointer<AVFrame> AVFramePtr;

// Wraps an AVFrame that the caller is handing over, freeing it once the last
// reference goes away.
AVFramePtr wrapAVFrame(AVFrame *frame);

// The one VAAPI device the whole client shares. Encoder, decoder and renderer
// must all work against the same VADisplay - surfaces created on one display
// cannot be used on another - so the device is created once, on first use, and
// handed out from here.
class VaapiContext
{
public:
	// A new reference to the shared device context, for an encoder's or
	// decoder's hw_device_ctx. The caller owns the returned reference and
	// unrefs it as usual. Null if VAAPI is unavailable.
	static AVBufferRef *deviceRef();

	// The underlying VADisplay, needed to export a decoded surface for
	// display. Null if VAAPI is unavailable.
	static VADisplay display();

	// Whether a VAAPI device could be opened at all. Callers use this to fall
	// back to software rather than each probing for themselves.
	static bool isAvailable();

	// Whether the display was opened on an X11 connection rather than a DRM
	// render node. That is decided once, when the device is created, and
	// decides how a decoded surface can be drawn: only an X11 display can be
	// drawn with vaPutSurface(), which is the only option left when Qt's
	// OpenGL contexts come from GLX. Always false in a build without X11
	// rendering support.
	static bool usesX11();

	// The X connection the display was opened on, as a Display*, or null when
	// it was not opened on one. Untyped to keep the Xlib headers, and the
	// names they take over, out of everything that includes this.
	static void *x11Display();

	// Registers the metatype for AVFramePtr, so hardware frames can be passed
	// through queued signal connections. Call once at startup.
	static void registerMetaTypes();

	// Tells Xlib that its connection will be used from more than one thread,
	// which it only accepts before that connection is opened - so this has to
	// run before QApplication is constructed. Does nothing in a build without
	// X11 rendering support.
	static void initX11Threading();

	// Works out whether this session needs the X11 drawing path - that is,
	// whether Qt's OpenGL contexts come from GLX, leaving no way to import a
	// surface as an EGL image - and if so remembers Qt's X connection to open
	// the VAAPI display on. Call once at startup, after QApplication exists
	// and before any video starts. Does nothing outside an X11 session, or
	// when Qt is using EGL there.
	static void detectX11Display();
};

class QWidget;

// The common face of the two widgets that can draw a decoded surface: the
// OpenGL one, which imports it as EGL images, and the X11 one, which has the
// driver put it on screen. Which of them a session can use is not known until
// it runs, and nothing outside create() should care which it got.
class VaapiRenderWidget
{
public:
	virtual ~VaapiRenderWidget() {}

	// The widget to lay out, show and hide, and to connect to: both carry a
	// renderingFailed(QString) signal.
	virtual QWidget *widget() = 0;

	// Takes the frame to draw next, replacing any previous one not yet drawn -
	// for live video the newest frame is the one that matters.
	virtual void setFrame(AVFramePtr frame) = 0;

	// Drops the current frame and clears the widget.
	virtual void clearFrame() = 0;

	// Whether frames are still being drawn. False once drawing has failed, so
	// a caller can fall back to the software path rather than leave a blank
	// widget.
	virtual bool isWorking() const = 0;

	// The renderer this session can use. Never null - it falls back to the
	// OpenGL one, which reports the reason it cannot draw if it cannot.
	static VaapiRenderWidget *create(QWidget *parent);

	// Whether drawing GPU frames has already been found not to work in this
	// session. Nothing about that changes while the client runs - it follows
	// from the kind of OpenGL context Qt provides and what the driver can do
	// with it - so once one widget has failed, the next will fail the same
	// way. Callers use this to stop asking for GPU frames at all rather than
	// to fail again for every window they open.
	static bool renderingKnownBroken() { return s_renderingBroken; }

protected:
	static void markRenderingBroken() { s_renderingBroken = true; }

private:
	static bool s_renderingBroken;
};

Q_DECLARE_METATYPE(AVFramePtr)

#endif // HAVE_VAAPI

#endif // VAAPI_H
