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

#include "videoscaler.h"

#include <QDebug>

// FFmpeg headers
extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

// The QImage formats libswscale can be pointed at directly. Anything missing
// here is not an error - scale() just uses Qt for those frames. Premultiplied
// formats are deliberately absent: libswscale has no equivalent, and feeding
// it premultiplied pixels as if they were plain ones would wash the colours
// out.
static AVPixelFormat av_pixel_format(QImage::Format format)
{
	switch (format)
	{
	case QImage::Format_RGB888:
		return AV_PIX_FMT_RGB24;
	case QImage::Format_RGB32:
		return AV_PIX_FMT_0RGB32;
	case QImage::Format_ARGB32:
		return AV_PIX_FMT_RGB32;
	case QImage::Format_RGBX8888:
		return AV_PIX_FMT_RGB0;
	case QImage::Format_RGBA8888:
		return AV_PIX_FMT_RGBA;
	case QImage::Format_Grayscale8:
		return AV_PIX_FMT_GRAY8;
	default:
		return AV_PIX_FMT_NONE;
	}
}

VideoScaler::VideoScaler(Backend backend)
	: m_backend(backend), m_lastUsed(backend), m_sws(nullptr),
	  m_srcWidth(0), m_srcHeight(0), m_srcFormat(AV_PIX_FMT_NONE),
	  m_dstWidth(0), m_dstHeight(0), m_loggedFailure(false)
{
}

VideoScaler::~VideoScaler()
{
	if (m_sws)
		sws_freeContext(m_sws);
}

QSize VideoScaler::fitted(QSize frame, QSize area)
{
	if (frame.isEmpty() || area.isEmpty())
		return QSize();

	QSize result = frame;
	result.scale(area, Qt::KeepAspectRatio);

	// An odd width costs libswscale a factor of four and a half, because it
	// drops off its SIMD path and converts the picture a pixel at a time -
	// measured, and far and away the largest effect of any of the geometry
	// here: destination stride alignment and an odd height both turned out not
	// to matter at all. Window widths being whatever the user drags them to,
	// half of them would land on the slow path.
	//
	// So the width is rounded down to even. That loses at most one pixel of
	// picture and bends the aspect ratio by well under a tenth of a percent,
	// which is less than the rounding above has already done.
	//
	// It matters that this is the one place the target size is decided.
	// VideoDevice::Play() sizes the frames it produces by it and CallWindow
	// checks arriving frames against it - if the two disagreed by a pixel, the
	// window would resize every frame instead of none of them.
	result.setWidth(result.width() & ~1);

	// A picture far thinner than the area it goes into can round down to
	// nothing, and neither scaler accepts a zero sized target.
	if (result.width() < 2)
		result.setWidth(2);
	if (result.height() < 1)
		result.setHeight(1);

	return result;
}

QImage VideoScaler::scale(const QImage &frame, QSize area)
{
	QSize target = fitted(frame.size(), area);

	if (target.isEmpty())
		return QImage();

	if (m_backend == Swscale)
	{
		QImage scaled = scaleWithSwscale(frame, target);

		if (!scaled.isNull())
		{
			m_lastUsed = Swscale;
			return scaled;
		}
	}

	m_lastUsed = QtScaled;
	return scaleWithQt(frame, target);
}

QImage VideoScaler::scaleWithSwscale(const QImage &frame, QSize target)
{
	AVPixelFormat srcFormat = av_pixel_format(frame.format());

	if (srcFormat == AV_PIX_FMT_NONE)
		return QImage();

	if (!m_sws || frame.width() != m_srcWidth || frame.height() != m_srcHeight ||
	    (int)srcFormat != m_srcFormat || target.width() != m_dstWidth ||
	    target.height() != m_dstHeight)
	{
		if (m_sws)
		{
			sws_freeContext(m_sws);
			m_sws = nullptr;
		}

		// SWS_BILINEAR to match what Qt's SmoothTransformation looks
		// like, and what the playback path already converts colours
		// with in videodevice.cpp.
		m_sws = sws_getContext(frame.width(), frame.height(), srcFormat,
		                       target.width(), target.height(), AV_PIX_FMT_0RGB32,
		                       SWS_BILINEAR, nullptr, nullptr, nullptr);

		if (!m_sws)
		{
			if (!m_loggedFailure)
			{
				qWarning() << "Could not set up video display scaler, using Qt instead";
				m_loggedFailure = true;
			}
			return QImage();
		}

		m_srcWidth = frame.width();
		m_srcHeight = frame.height();
		m_srcFormat = (int)srcFormat;
		m_dstWidth = target.width();
		m_dstHeight = target.height();

		// AV_PIX_FMT_0RGB32 is defined to be exactly this layout on
		// either endianness, so libswscale can write into the image
		// without a conversion in between - and Qt draws a 32 bit
		// image without one either.
		m_dst = QImage(target.width(), target.height(), QImage::Format_RGB32);

		if (m_dst.isNull())
			return QImage();
	}

	const uint8_t *srcData[4] = { frame.constBits(), nullptr, nullptr, nullptr };
	int srcStride[4] = { (int)frame.bytesPerLine(), 0, 0, 0 };

	// bits() would deep copy if m_dst were shared with anyone, which is why
	// what scale() hands back only points at this buffer instead of sharing
	// it.
	uint8_t *dstData[4] = { m_dst.bits(), nullptr, nullptr, nullptr };
	int dstStride[4] = { (int)m_dst.bytesPerLine(), 0, 0, 0 };

	if (sws_scale(m_sws, srcData, srcStride, 0, frame.height(), dstData, dstStride) <= 0)
		return QImage();

	return QImage(m_dst.constBits(), m_dst.width(), m_dst.height(),
	              m_dst.bytesPerLine(), m_dst.format());
}

QImage VideoScaler::scaleWithQt(const QImage &frame, QSize target)
{
	// The aspect ratio is already accounted for in target, so scaling to it
	// exactly avoids rounding the size a second time.
	return frame.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
