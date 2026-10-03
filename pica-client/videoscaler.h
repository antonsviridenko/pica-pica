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
#ifndef VIDEOSCALER_H
#define VIDEOSCALER_H

#include <QImage>
#include <QSize>

struct SwsContext;

// Decides what size a video frame should be drawn at, and resizes one when
// something else has not already done it.
//
// Frames arrive at whatever resolution the sender negotiated, which has nothing
// to do with the size of the window they end up in, so each one has to be
// resized before it is drawn - at the frame rate of the call, for as long as it
// lasts. Where that resizing happens turned out to matter a great deal, and
// tests/test_videoscaler is the measurement it was settled by:
//
//   - Resizing packed RGB in the window, which is what used to happen, is the
//     worst of the options. libswscale is especially bad at it - it has no
//     scaler for packed formats, so it unpacks to planar YUV, scales, and packs
//     the result back, losing to QImage::scaled() by four to five times.
//
//   - Resizing inside the colour conversion the decode loop already runs is
//     between 1.1 and 4.6 times faster than the whole two pass pipeline, best
//     when the picture is being shrunk, since nothing is converted only to be
//     thrown away. libswscale is good at this one: the source is planar YUV,
//     which is what it wants, and the resize rides along inside a pass over
//     the picture that had to happen anyway.
//
//   - Handing the result over without copying it saves another pass again. See
//     VideoDevice::Play(), which allocates each frame's QImage rather than
//     reusing one buffer, so the queued signal that carries it to the GUI
//     thread only has a reference count to bump.
//
// So VideoDevice::Play() does the resizing, and this class is left with two
// jobs. fitted() is the single authority on what size to aim for, shared so
// that the decoder and the window cannot disagree about it. scale() is the
// window's fallback for the frames that arrive at the wrong size anyway -
// during a resize, there are always a few already in flight - and is a no-op
// when the size is already right, which is the steady state.
//
// The libswscale backend is kept for the benchmark to compare against, and to
// keep the conclusion above honest if any of these libraries change. It only
// understands a handful of QImage formats; anything else, and any failure to
// build a scaler, falls back to Qt rather than losing the picture.
class VideoScaler
{
public:
	// Not named after Qt itself - a value called Qt would shadow the
	// namespace of the same name inside this class.
	enum Backend
	{
		QtScaled,	// QImage::scaled(), the default - measurably the faster one here
		Swscale		// libswscale, for comparison; falls back to the above when it cannot help
	};

	explicit VideoScaler(Backend backend = QtScaled);
	~VideoScaler();

	// The size `frame` becomes when fitted into `area` without cropping or
	// changing its aspect ratio - never larger than `area`, and never
	// smaller than 2x1 so long as both arguments are valid. An empty size
	// if either argument is empty.
	//
	// The width is always even, which is worth a factor of four and a half
	// downstream; the reason is with the implementation. Every part of the
	// call that needs to know the display size of a frame goes through here,
	// so that they all arrive at the same answer.
	static QSize fitted(QSize frame, QSize area);

	// Returns `frame` resized to fitted(frame.size(), area), ready to be
	// handed to QPixmap::fromImage(). A null image means nothing could be
	// produced and the previous picture should be left alone.
	//
	// The returned image may reference a buffer this object reuses, so it
	// is only valid until the next call to scale() - copy it if it needs to
	// outlive that.
	QImage scale(const QImage &frame, QSize area);

	// Which implementation the last scale() actually used. Not the same as
	// the backend asked for: Swscale falls back to QtScaled when it meets a
	// frame format it has no mapping for.
	Backend lastBackendUsed() const { return m_lastUsed; }

private:
	QImage scaleWithSwscale(const QImage &frame, QSize target);
	static QImage scaleWithQt(const QImage &frame, QSize target);

	Backend m_backend;
	Backend m_lastUsed;

	// Building a scaler means computing filter tables, so the context is
	// kept across frames and only rebuilt when the geometry or the format
	// it was built for changes - in practice on the first frame and on a
	// window resize.
	SwsContext *m_sws;
	int m_srcWidth;
	int m_srcHeight;
	int m_srcFormat;	// AVPixelFormat, kept as int to keep ffmpeg out of this header
	int m_dstWidth;
	int m_dstHeight;

	// Destination of the libswscale path, reused frame to frame. Qt images
	// are copy on write, and writing through the raw pointer libswscale is
	// given would go behind that, so scale() makes sure this is not shared
	// before handing the pointer over.
	QImage m_dst;

	bool m_loggedFailure;
};

#endif
