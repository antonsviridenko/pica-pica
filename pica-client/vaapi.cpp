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

#include <QMutex>
#include <QMutexLocker>
#include <QDebug>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/error.h>
}

namespace
{
	QMutex g_mutex;
	// Created on first use and kept for the lifetime of the process: opening a
	// VAAPI device is not cheap, and every user of it has to share the one
	// display anyway.
	AVBufferRef *g_device = nullptr;
	bool g_tried = false;

	// Must be called with g_mutex held.
	void ensureDevice()
	{
		if (g_tried)
			return;

		g_tried = true;

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

void VaapiContext::registerMetaTypes()
{
	qRegisterMetaType<AVFramePtr>("AVFramePtr");
}

#endif // HAVE_VAAPI
