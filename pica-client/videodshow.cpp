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
#include "mediadevice.h"
#include "videodevice.h"

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QDebug>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <functional>

// INITGUID before the DirectShow headers so CLSID_SystemDeviceEnum and the
// rest get defined here rather than needing strmiids at link time. The GUIDs
// are emitted with DECLSPEC_SELECTANY, so the ones this shares with
// nativeaudio_wasapi.cpp merge rather than clashing.
#define INITGUID
#include <initguid.h>

#include <windows.h>
#include <objbase.h>
#include <dshow.h>
// VIDEOINFOHEADER2 lives here rather than in dshow.h, which only brings in the
// FORMAT_VideoInfo2 GUID that names it.
#include <dvdmedia.h>

static QString wideToQString(LPCWSTR s)
{
	return s ? QString::fromWCharArray(s) : QString();
}

// Compressed formats worth forwarding untouched, best first. Same order and
// the same FFmpeg codec names as kCompressedFormatPreference in
// videodevice.cpp - that table is keyed by v4l2 pixel format, this one by the
// FourCC DirectShow reports in BITMAPINFOHEADER::biCompression, but they mean
// the same thing and should be kept in step.
//
// Cameras are inconsistent about which FourCC they use for the same stream,
// so several map to one codec.
static const struct
{
	const char *fourcc;
	const char *ffmpeg_codec;
} kDshowCompressedFormats[] =
{
	{ "HEVC", "hevc"  },
	{ "hevc", "hevc"  },
	{ "H265", "hevc"  },
	{ "h265", "hevc"  },
	{ "H264", "h264"  },
	{ "h264", "h264"  },
	{ "X264", "h264"  },
	{ "x264", "h264"  },
	{ "AVC1", "h264"  },
	{ "avc1", "h264"  },
	{ "MJPG", "mjpeg" },
	{ "mjpg", "mjpeg" },
	{ "dmb1", "mjpeg" },
	{ "JPEG", "mjpeg" },
};

static QString fourccToString(DWORD fourcc)
{
	char c[5];
	c[0] = (char)(fourcc & 0xff);
	c[1] = (char)((fourcc >> 8) & 0xff);
	c[2] = (char)((fourcc >> 16) & 0xff);
	c[3] = (char)((fourcc >> 24) & 0xff);
	c[4] = 0;

	// Uncompressed types use small numbers rather than four characters -
	// BI_RGB is 0 - which would otherwise come out as an unreadable string.
	for (int i = 0; i < 4; i++)
	{
		if (c[i] < 0x20 || c[i] > 0x7e)
			return QString("0x%1").arg((quint32)fourcc, 8, 16, QLatin1Char('0'));
	}

	return QString::fromLatin1(c);
}

// AM_MEDIA_TYPE owns two allocations of its own. DeleteMediaType() would do
// this, but it lives in the DirectShow base classes rather than in any library
// worth pulling in for one function.
static void freeMediaType(AM_MEDIA_TYPE *mt)
{
	if (!mt)
		return;

	if (mt->cbFormat && mt->pbFormat)
		CoTaskMemFree(mt->pbFormat);
	if (mt->pUnk)
		mt->pUnk->Release();

	CoTaskMemFree(mt);
}

// Finds the camera behind one of the device strings enumeration handed out,
// matching on either name for the same reason FFmpeg's demuxer does: the
// friendly name for the common case, the device path where that was not
// unique. Caller releases the moniker.
static IMoniker *findCamera(const QString &device)
{
	ICreateDevEnum *devEnum = nullptr;
	if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
	                            IID_ICreateDevEnum, (void **)&devEnum)) || !devEnum)
		return nullptr;

	IEnumMoniker *enumMoniker = nullptr;
	HRESULT hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
	devEnum->Release();

	if (hr != S_OK || !enumMoniker)
		return nullptr;

	IMoniker *moniker = nullptr;
	IMoniker *found = nullptr;

	while (!found && enumMoniker->Next(1, &moniker, nullptr) == S_OK)
	{
		IPropertyBag *bag = nullptr;
		if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void **)&bag)) && bag)
		{
			VARIANT var;

			VariantInit(&var);
			if (SUCCEEDED(bag->Read(L"FriendlyName", &var, nullptr)) && var.vt == VT_BSTR &&
			    wideToQString(var.bstrVal) == device)
			{
				found = moniker;
			}
			VariantClear(&var);

			if (!found)
			{
				VariantInit(&var);
				if (SUCCEEDED(bag->Read(L"DevicePath", &var, nullptr)) && var.vt == VT_BSTR &&
				    wideToQString(var.bstrVal).replace(QLatin1Char(':'), QLatin1Char('_')) == device)
				{
					found = moniker;
				}
				VariantClear(&var);
			}

			bag->Release();
		}

		if (!found)
			moniker->Release();
	}

	enumMoniker->Release();
	return found;
}

// Which compressed formats a camera can hand over ready-made, best first.
//
// The counterpart of the v4l2 VIDIOC_ENUM_FMT walk in
// VideoDevice::CompressedFormats(). DirectShow has no single call for it: the
// camera has to be instantiated, its output pin found, and the pin's stream
// capabilities enumerated one media type at a time.
//
// Note this instantiates the capture filter, which is also the step that fails
// when Windows is refusing the app access to the camera - so an empty list
// here can mean either "offers nothing compressed" or "cannot be opened at
// all". Both lead to the same place, the caller simply transcodes instead.
QStringList pica_dshow_compressed_formats(const QString &device)
{
	QStringList result;

	if (device.isEmpty())
		return result;

	bool didInit = false;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr))
		didInit = true;
	else if (hr != RPC_E_CHANGED_MODE)
		return result;

	IMoniker *moniker = findCamera(device);
	if (!moniker)
	{
		if (didInit)
			CoUninitialize();
		return result;
	}

	IBaseFilter *filter = nullptr;
	hr = moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void **)&filter);
	moniker->Release();

	if (FAILED(hr) || !filter)
	{
		qWarning() << "DirectShow: could not instantiate" << device
		           << "to ask what formats it offers";
		if (didInit)
			CoUninitialize();
		return result;
	}

	QStringList offered;

	IEnumPins *enumPins = nullptr;
	if (SUCCEEDED(filter->EnumPins(&enumPins)) && enumPins)
	{
		IPin *pin = nullptr;

		while (enumPins->Next(1, &pin, nullptr) == S_OK)
		{
			PIN_DIRECTION dir;
			IAMStreamConfig *config = nullptr;

			if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == PINDIR_OUTPUT &&
			    SUCCEEDED(pin->QueryInterface(IID_IAMStreamConfig, (void **)&config)) && config)
			{
				int count = 0, size = 0;

				if (SUCCEEDED(config->GetNumberOfCapabilities(&count, &size)) && size > 0)
				{
					QVector<quint8> caps(size);

					for (int i = 0; i < count; i++)
					{
						AM_MEDIA_TYPE *mt = nullptr;
						if (FAILED(config->GetStreamCaps(i, &mt, caps.data())) || !mt)
							continue;

						// Only the VIDEOINFOHEADER shapes carry a
						// BITMAPINFOHEADER, which is where the FourCC lives.
						const BITMAPINFOHEADER *bih = nullptr;

						if (mt->formattype == FORMAT_VideoInfo &&
						    mt->cbFormat >= sizeof(VIDEOINFOHEADER))
							bih = &((VIDEOINFOHEADER *)mt->pbFormat)->bmiHeader;
						else if (mt->formattype == FORMAT_VideoInfo2 &&
						         mt->cbFormat >= sizeof(VIDEOINFOHEADER2))
							bih = &((VIDEOINFOHEADER2 *)mt->pbFormat)->bmiHeader;

						if (bih)
						{
							QString fourcc = fourccToString(bih->biCompression);
							if (!offered.contains(fourcc))
								offered << fourcc;
						}

						freeMediaType(mt);
					}
				}
			}

			if (config)
				config->Release();
			pin->Release();
		}

		enumPins->Release();
	}

	filter->Release();

	// Worth logging whole, not just the ones we recognise. A camera offering
	// nothing but uncompressed types here - NV12, YUY2 - while advertising
	// MJPEG on the same hardware under v4l2 is the signature of the Windows
	// Camera Frame Server, which mediates integrated cameras and hands
	// DirectShow the already-decoded output. Nothing to be done about that
	// from here, but it is worth being able to tell it apart from a FourCC we
	// simply failed to map.
	qDebug() << "DirectShow:" << device << "offers"
	         << (offered.isEmpty() ? QStringLiteral("(nothing)") : offered.join(QLatin1String(", ")));

	// Report in our preference order rather than in the order the camera
	// happened to list them.
	for (unsigned int i = 0; i < sizeof(kDshowCompressedFormats) / sizeof(kDshowCompressedFormats[0]); i++)
	{
		QString codec = QLatin1String(kDshowCompressedFormats[i].ffmpeg_codec);

		if (offered.contains(QLatin1String(kDshowCompressedFormats[i].fourcc)) && !result.contains(codec))
			result << codec;
	}

	if (didInit)
		CoUninitialize();

	return result;
}

// Frame rates worth offering out of a pin that reports a range of frame
// intervals rather than one fixed rate. Same list, and the same reasoning, as
// kStandardFrameRates in videodevice.cpp.
static const int kDshowStandardFrameRates[] = { 60, 50, 30, 25, 24, 20, 15, 10, 5 };

// DirectShow measures a frame interval in 100 nanosecond units, and we want
// the rate, so this is a reciprocal like the v4l2 one.
static double intervalToFps(LONGLONG interval)
{
	if (interval <= 0)
		return 0.0;

	return 10000000.0 / (double)interval;
}

// Largest picture first - the same order VideoDevice::CaptureFormats() reports
// on Linux, so the settings dialog's list reads the same on both.
static bool dshowFormatLessThan(const VideoCaptureFormat &a, const VideoCaptureFormat &b)
{
	const qint64 areaA = (qint64)a.width * a.height;
	const qint64 areaB = (qint64)b.width * b.height;

	if (areaA != areaB)
		return areaA > areaB;

	return a.width > b.width;
}

// Every picture size the camera can deliver, with the frame rates and
// compressed formats available at each.
//
// The counterpart of the v4l2 VIDIOC_ENUM_FRAMESIZES/VIDIOC_ENUM_FRAMEINTERVALS
// walk in VideoDevice::CaptureFormats(), and it comes off the same
// IAMStreamConfig enumeration pica_dshow_compressed_formats() uses: each media
// type carries a size and a FourCC, and the VIDEO_STREAM_CONFIG_CAPS alongside
// it carries the range of frame intervals that size can be delivered at.
//
// The same caveat applies as there: this instantiates the capture filter, so
// an empty list can mean "cannot be opened at all" as much as "offers
// nothing". Either way the caller falls back to a set of common sizes.
QList<VideoCaptureFormat> pica_dshow_capture_formats(const QString &device)
{
	QList<VideoCaptureFormat> result;

	if (device.isEmpty())
		return result;

	bool didInit = false;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr))
		didInit = true;
	else if (hr != RPC_E_CHANGED_MODE)
		return result;

	IMoniker *moniker = findCamera(device);
	if (!moniker)
	{
		if (didInit)
			CoUninitialize();
		return result;
	}

	IBaseFilter *filter = nullptr;
	hr = moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void **)&filter);
	moniker->Release();

	if (FAILED(hr) || !filter)
	{
		qWarning() << "DirectShow: could not instantiate" << device
		           << "to ask what sizes it offers";
		if (didInit)
			CoUninitialize();
		return result;
	}

	// Keyed on the size, because a camera lists one media type per size and
	// format combination and the same size comes back once per format.
	QMap<QPair<int, int>, VideoCaptureFormat> bySize;

	IEnumPins *enumPins = nullptr;
	if (SUCCEEDED(filter->EnumPins(&enumPins)) && enumPins)
	{
		IPin *pin = nullptr;

		while (enumPins->Next(1, &pin, nullptr) == S_OK)
		{
			PIN_DIRECTION dir;
			IAMStreamConfig *config = nullptr;

			if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == PINDIR_OUTPUT &&
			    SUCCEEDED(pin->QueryInterface(IID_IAMStreamConfig, (void **)&config)) && config)
			{
				int count = 0, size = 0;

				if (SUCCEEDED(config->GetNumberOfCapabilities(&count, &size)) &&
				    size >= (int)sizeof(VIDEO_STREAM_CONFIG_CAPS))
				{
					QVector<quint8> caps(size);

					for (int i = 0; i < count; i++)
					{
						AM_MEDIA_TYPE *mt = nullptr;
						if (FAILED(config->GetStreamCaps(i, &mt, caps.data())) || !mt)
							continue;

						const BITMAPINFOHEADER *bih = nullptr;
						// The nominal interval of this particular media type,
						// as opposed to the range in the caps below.
						LONGLONG avgTimePerFrame = 0;

						if (mt->formattype == FORMAT_VideoInfo &&
						    mt->cbFormat >= sizeof(VIDEOINFOHEADER))
						{
							const VIDEOINFOHEADER *vih = (const VIDEOINFOHEADER *)mt->pbFormat;
							bih = &vih->bmiHeader;
							avgTimePerFrame = vih->AvgTimePerFrame;
						}
						else if (mt->formattype == FORMAT_VideoInfo2 &&
						         mt->cbFormat >= sizeof(VIDEOINFOHEADER2))
						{
							const VIDEOINFOHEADER2 *vih = (const VIDEOINFOHEADER2 *)mt->pbFormat;
							bih = &vih->bmiHeader;
							avgTimePerFrame = vih->AvgTimePerFrame;
						}

						if (!bih || bih->biWidth <= 0 || bih->biHeight == 0)
						{
							freeMediaType(mt);
							continue;
						}

						// A bottom-up DIB states its height negative; the
						// picture is the same size either way.
						const int width = (int)bih->biWidth;
						const int height = bih->biHeight < 0 ? (int)-bih->biHeight : (int)bih->biHeight;

						VideoCaptureFormat &entry = bySize[qMakePair(width, height)];
						entry.width = width;
						entry.height = height;

						const QString fourcc = fourccToString(bih->biCompression);

						for (unsigned int f = 0; f < sizeof(kDshowCompressedFormats) / sizeof(kDshowCompressedFormats[0]); f++)
						{
							if (fourcc != QLatin1String(kDshowCompressedFormats[f].fourcc))
								continue;

							QString codec = QLatin1String(kDshowCompressedFormats[f].ffmpeg_codec);

							if (!entry.compressedFormats.contains(codec))
								entry.compressedFormats << codec;

							break;
						}

						const VIDEO_STREAM_CONFIG_CAPS *scc = (const VIDEO_STREAM_CONFIG_CAPS *)caps.constData();

						// The shortest interval is the highest rate, hence the
						// crossed over names.
						const double maxFps = intervalToFps(scc->MinFrameInterval);
						const double minFps = intervalToFps(scc->MaxFrameInterval);

						int nominal = qRound(intervalToFps(avgTimePerFrame));

						if (nominal > 0 && !entry.frameRates.contains(nominal))
							entry.frameRates << nominal;

						// A pin that accepts a range of intervals will take
						// anything within it, so there is no list to show -
						// the standard rates inside the range stand in for one,
						// exactly as on the v4l2 side.
						for (unsigned int r = 0; r < sizeof(kDshowStandardFrameRates) / sizeof(kDshowStandardFrameRates[0]); r++)
						{
							const int fps = kDshowStandardFrameRates[r];

							if (fps >= minFps && fps <= maxFps && !entry.frameRates.contains(fps))
								entry.frameRates << fps;
						}

						freeMediaType(mt);
					}
				}
			}

			if (config)
				config->Release();
			pin->Release();
		}

		enumPins->Release();
	}

	filter->Release();

	for (QMap<QPair<int, int>, VideoCaptureFormat>::iterator it = bySize.begin(); it != bySize.end(); ++it)
	{
		VideoCaptureFormat entry = it.value();

		// Report the formats in our preference order rather than in the order
		// the camera happened to list them, the same way
		// pica_dshow_compressed_formats() does.
		QStringList ordered;

		for (unsigned int f = 0; f < sizeof(kDshowCompressedFormats) / sizeof(kDshowCompressedFormats[0]); f++)
		{
			QString codec = QLatin1String(kDshowCompressedFormats[f].ffmpeg_codec);

			if (entry.compressedFormats.contains(codec) && !ordered.contains(codec))
				ordered << codec;
		}

		entry.compressedFormats = ordered;

		std::sort(entry.frameRates.begin(), entry.frameRates.end(), std::greater<int>());

		result << entry;
	}

	std::sort(result.begin(), result.end(), dshowFormatLessThan);

	if (didInit)
		CoUninitialize();

	return result;
}

// Lists the cameras DirectShow knows about.
//
// Capture itself goes through FFmpeg's "dshow" demuxer rather than through
// DirectShow directly - it does the job and there is no equivalent of the
// audio situation here, where the platform had something we could not reach
// any other way. But the demuxer has no enumeration API: it can only print
// device names to the log with -list_devices, which is no use to a settings
// dialog. So the list is built here, from the same device category the
// demuxer itself walks, and the names handed back are the ones it matches on.
QList<MediaDeviceInfo> pica_enumerate_dshow_video()
{
	QList<MediaDeviceInfo> result;

	// Qt has already put the GUI thread into an apartment; the reference is
	// still ours to balance, hence the matching CoUninitialize() below.
	bool didInit = false;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr))
		didInit = true;
	else if (hr != RPC_E_CHANGED_MODE)
		return result;

	ICreateDevEnum *devEnum = nullptr;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
	                      IID_ICreateDevEnum, (void **)&devEnum);
	if (FAILED(hr) || !devEnum)
	{
		qWarning() << "DirectShow: could not create the device enumerator"
		           << QString("0x%1").arg((quint32)hr, 8, 16, QLatin1Char('0'));
		if (didInit)
			CoUninitialize();
		return result;
	}

	IEnumMoniker *enumMoniker = nullptr;
	hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);

	// S_FALSE with a null enumerator is how "no cameras at all" comes back.
	if (hr != S_OK || !enumMoniker)
	{
		devEnum->Release();
		if (didInit)
			CoUninitialize();
		return result;
	}

	int index = 0;
	QStringList seenNames;
	IMoniker *moniker = nullptr;

	while (enumMoniker->Next(1, &moniker, nullptr) == S_OK)
	{
		IPropertyBag *bag = nullptr;
		if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void **)&bag)) || !bag)
		{
			moniker->Release();
			continue;
		}

		QString friendlyName;
		QString uniqueName;

		VARIANT var;
		VariantInit(&var);
		if (SUCCEEDED(bag->Read(L"FriendlyName", &var, nullptr)) && var.vt == VT_BSTR)
			friendlyName = wideToQString(var.bstrVal);
		VariantClear(&var);

		VariantInit(&var);
		if (SUCCEEDED(bag->Read(L"DevicePath", &var, nullptr)) && var.vt == VT_BSTR)
		{
			// FFmpeg calls this the device's "alternative name" and matches on
			// it as well as on the friendly one, after replacing colons -
			// which it uses as a separator in its own device strings.
			uniqueName = wideToQString(var.bstrVal).replace(QLatin1Char(':'), QLatin1Char('_'));
		}
		VariantClear(&var);

		bag->Release();
		moniker->Release();

		if (friendlyName.isEmpty())
			continue;

		MediaDeviceInfo d;
		d.humanReadable = friendlyName;

		// The friendly name is the documented way to name a dshow device and
		// the one to prefer. It is not required to be unique though, and two
		// identical cameras would both answer to it - the demuxer would open
		// whichever came first. Later duplicates therefore fall back to the
		// device path, which is unique by construction.
		if (!seenNames.contains(friendlyName))
		{
			d.device = friendlyName;
			seenNames << friendlyName;
		}
		else if (!uniqueName.isEmpty())
		{
			d.device = uniqueName;
			d.humanReadable += QString(" (%1)").arg(seenNames.count(friendlyName) + 1);
			seenNames << friendlyName;
		}
		else
		{
			// Same name, no device path to tell them apart. Listing it again
			// would just give the user two entries that open the same camera.
			continue;
		}

		d.index = index++;

		// Nothing here about what the camera can deliver: that means walking
		// its output pin's media types, which pica_dshow_capture_formats()
		// does once a camera has actually been selected rather than for every
		// camera on the machine.
		result << d;
	}

	enumMoniker->Release();
	devEnum->Release();

	if (didInit)
		CoUninitialize();

	return result;
}

#endif // Q_OS_WIN
