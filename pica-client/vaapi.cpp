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
#include "vaapi.h"

#ifdef HAVE_VAAPI

#include "vaapivideowidget.h"
#ifdef HAVE_VAAPI_X11
#include "vaapix11widget.h"
#endif

#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#ifdef HAVE_VAAPI_X11
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#endif

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/error.h>
}

#ifdef HAVE_VAAPI_X11
// Included after the Qt headers, since Xlib defines a handful of names as
// macros that Qt also uses as ordinary identifiers.
#include <X11/Xlib.h>
#include <GL/glx.h>
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
#endif

namespace
{
	QMutex g_mutex;
	// Created on first use and kept for the lifetime of the process: opening a
	// VAAPI device is not cheap, and every user of it has to share the one
	// display anyway.
	AVBufferRef *g_device = nullptr;
	bool g_tried = false;
	bool g_usesX11 = false;

#ifdef HAVE_VAAPI_X11
	// Qt's own X connection, found at startup by detectX11Display(), and only
	// when Qt's OpenGL contexts turned out to be GLX ones. Owned by Qt.
	//
	// Using Qt's connection rather than one of our own means the windows the
	// driver is asked to draw on are already known to the server on the same
	// connection, with nothing to flush or synchronise first.
	Display *g_x11Display = nullptr;

	// Hands an already opened VADisplay to FFmpeg. Unlike
	// av_hwdevice_ctx_create() this leaves the display in our hands rather
	// than opening one of its own, which is the whole point here: the renderer
	// has to draw surfaces on the very same display the decoder made them on.
	AVBufferRef *wrapVaDisplay(VADisplay va_display)
	{
		AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);

		if (!ref)
			return nullptr;

		AVHWDeviceContext *devctx = (AVHWDeviceContext *)ref->data;
		AVVAAPIDeviceContext *vactx = (AVVAAPIDeviceContext *)devctx->hwctx;

		vactx->display = va_display;

		if (av_hwdevice_ctx_init(ref) < 0)
		{
			av_buffer_unref(&ref);
			return nullptr;
		}

		return ref;
	}

	// Opens the VAAPI device on the X connection, so that decoded surfaces can
	// later be drawn with vaPutSurface(). Returns null when that is not the way
	// this session draws - under Wayland, or under X11 with Qt on EGL, where a
	// surface is drawn through its DMA-BUF handles instead - and on any
	// failure, leaving the caller to open the device through a DRM render node.
	AVBufferRef *createX11Device()
	{
		if (!g_x11Display)
			return nullptr;

		// libva's X11 backend prefers DRI3, which gets it a DRM file
		// descriptor and nothing else. That is all decoding needs, but drawing
		// is another matter: i965's vaPutSurface() draws into DRI2 buffers and
		// answers VA_STATUS_ERROR_UNKNOWN, silently and with nothing logged at
		// any verbosity, for every frame when the display was not opened that
		// way. So ask libva for DRI2.
		//
		// Set for the rest of the process rather than just around the calls
		// below, and deliberately so: libva reads it in
		// va_dri_get_rendering_buffer(), which is called while a surface is
		// being put on a window - restoring the environment after
		// vaInitialize() would leave the variable unset exactly when it is
		// consulted. Nothing else in the client opens an X11 VA display, and a
		// value the user set is left alone.
		//
		// Should DRI2 turn out not to be available, opening fails and the
		// caller falls back to a DRM render node, which never looks at this.
		if (!qEnvironmentVariableIsSet("LIBVA_DRI3_DISABLE"))
			qputenv("LIBVA_DRI3_DISABLE", "1");

		VADisplay va_display = vaGetDisplay(g_x11Display);

		if (!va_display)
		{
			qWarning() << "VAAPI: this libva build has no X11 support";
			return nullptr;
		}

		int major = 0, minor = 0;
		VAStatus st = vaInitialize(va_display, &major, &minor);

		if (st != VA_STATUS_SUCCESS)
		{
			qWarning() << "VAAPI: vaInitialize() on the X display failed:" << vaErrorStr(st);
			vaTerminate(va_display);
			return nullptr;
		}

		AVBufferRef *ref = wrapVaDisplay(va_display);

		if (!ref)
		{
			qWarning() << "VAAPI: the X display could not be handed to FFmpeg";
			vaTerminate(va_display);
			return nullptr;
		}

		g_usesX11 = true;

		return ref;
	}
#endif

	// Must be called with g_mutex held.
	void ensureDevice()
	{
		if (g_tried)
			return;

		g_tried = true;

#ifdef HAVE_VAAPI_X11
		// Where the widgets will be drawn on by the driver, the display has to
		// be opened on that same X connection - a surface belongs to the
		// display it was made on. Encoding and decoding work the same either
		// way, so nothing is given up by preferring it here.
		g_device = createX11Device();

		if (g_device)
			return;
#endif

		// A null device name lets FFmpeg find a usable DRM render node
		// (/dev/dri/renderD128 and friends) by itself.
		int ret = av_hwdevice_ctx_create(&g_device, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
		if (ret < 0)
		{
			char buf[256] = {0};
			av_strerror(ret, buf, sizeof(buf));
			qWarning() << "VAAPI is not usable, falling back to software video:" << buf;
			g_device = nullptr;
		}
	}
}

AVFramePtr wrapAVFrame(AVFrame *frame)
{
	return AVFramePtr(frame, [](AVFrame *f) { av_frame_free(&f); });
}

AVBufferRef *VaapiContext::deviceRef()
{
	QMutexLocker locker(&g_mutex);
	ensureDevice();

	return g_device ? av_buffer_ref(g_device) : nullptr;
}

VADisplay VaapiContext::display()
{
	QMutexLocker locker(&g_mutex);
	ensureDevice();

	if (!g_device)
		return nullptr;

	AVHWDeviceContext *devctx = (AVHWDeviceContext *)g_device->data;
	AVVAAPIDeviceContext *vactx = (AVVAAPIDeviceContext *)devctx->hwctx;

	return vactx->display;
}

bool VaapiContext::isAvailable()
{
	QMutexLocker locker(&g_mutex);
	ensureDevice();

	return g_device != nullptr;
}

bool VaapiContext::usesX11()
{
	QMutexLocker locker(&g_mutex);
	ensureDevice();

	return g_usesX11;
}

void *VaapiContext::x11Display()
{
#ifdef HAVE_VAAPI_X11
	QMutexLocker locker(&g_mutex);
	ensureDevice();

	return g_usesX11 ? (void *)g_x11Display : nullptr;
#else
	return nullptr;
#endif
}

void VaapiContext::registerMetaTypes()
{
	qRegisterMetaType<AVFramePtr>("AVFramePtr");
}

void VaapiContext::initX11Threading()
{
#ifdef HAVE_VAAPI_X11
	// Qt's X connection ends up being used from two threads: the GUI thread
	// drawing, and the decoding thread talking to libva, which talks to the
	// driver over that same connection. Xlib only handles that if it is told
	// before the connection is opened - hence being called ahead of
	// QApplication - and it is harmless when the session turns out not to be
	// an X11 one at all.
	XInitThreads();
#endif
}

void VaapiContext::detectX11Display()
{
#ifdef HAVE_VAAPI_X11
	QMutexLocker locker(&g_mutex);

	// Nothing to arrange once the device has been opened - it would be too
	// late to change which display its surfaces live on.
	if (g_tried || g_x11Display)
		return;

	if (QGuiApplication::platformName() != QLatin1String("xcb"))
		return;

	// A throwaway context, made only to see what Qt's OpenGL contexts are:
	// under X11 Qt uses GLX by default but EGL when asked to, and the two need
	// entirely different ways of getting a decoded surface onto the screen.
	// Every context in the process is made on the same X connection, so the
	// widget that draws video later will be on the one found here.
	//
	// A GLX context means the EGL import path is unavailable, and that is what
	// decides this: the surfaces have to go onto an X window instead.
	QOffscreenSurface surface;
	surface.create();

	if (!surface.isValid())
		return;

	QOpenGLContext ctx;

	if (!ctx.create() || !ctx.makeCurrent(&surface))
		return;

	Display *dpy = glXGetCurrentDisplay();

	ctx.doneCurrent();

	// Null means an EGL context, where the DMA-BUF path applies and there is
	// nothing to prepare.
	if (dpy)
		g_x11Display = dpy;
#endif
}

bool VaapiRenderWidget::s_renderingBroken = false;

VaapiRenderWidget *VaapiRenderWidget::create(QWidget *parent)
{
#ifdef HAVE_VAAPI_X11
	// Only when the display was opened on an X connection, which is also the
	// only case where a surface can be put onto a window.
	if (VaapiContext::usesX11())
		return new VaapiX11Widget(parent);
#endif

	return new VaapiVideoWidget(parent);
}

#endif // HAVE_VAAPI
