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

	// Registers the metatype for AVFramePtr, so hardware frames can be passed
	// through queued signal connections. Call once at startup.
	static void registerMetaTypes();
};

Q_DECLARE_METATYPE(AVFramePtr)

#endif // HAVE_VAAPI

#endif // VAAPI_H
