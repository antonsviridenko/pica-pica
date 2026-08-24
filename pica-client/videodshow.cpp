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

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QDebug>
#include <QList>
#include <QString>

// INITGUID before the DirectShow headers so CLSID_SystemDeviceEnum and the
// rest get defined here rather than needing strmiids at link time. The GUIDs
// are emitted with DECLSPEC_SELECTANY, so the ones this shares with
// nativeaudio_wasapi.cpp merge rather than clashing.
#define INITGUID
#include <initguid.h>

#include <windows.h>
#include <objbase.h>
#include <dshow.h>

static QString wideToQString(LPCWSTR s)
{
	return s ? QString::fromWCharArray(s) : QString();
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

		// Left empty deliberately. compressedFormats drives the "prefer
		// compressed formats" setting, and working out what a DirectShow
		// camera can deliver means walking its output pin's media types
		// through IAMStreamConfig - a good deal more work than the v4l2 call
		// that fills this in on Linux, and not needed to get a picture.
		result << d;
	}

	enumMoniker->Release();
	devEnum->Release();

	if (didInit)
		CoUninitialize();

	return result;
}

#endif // Q_OS_WIN
