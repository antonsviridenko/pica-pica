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
#include "capturegain.h"

#ifdef Q_OS_WIN

#include <QDebug>

// Deliberately NOT defining INITGUID here, unlike nativeaudio_wasapi.cpp: that
// file already emits the definitions for the endpoint GUIDs, and a second copy
// in this translation unit would collide at link time. The interface IDs below
// come from __uuidof() instead, which mingw-w64 supports because its headers
// declare these interfaces with __CRT_UUID_DECL.
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

static QString hrText(HRESULT hr)
{
	return QString("0x%1").arg((quint32)hr, 8, 16, QLatin1Char('0'));
}

// The endpoint volume, which is the slider under Settings > Sound > Input.
//
// Note this is not the same control as "Microphone Boost" - that one is a
// driver specific APO with no uniform API, and is usually the thing actually
// responsible when a mic clips at every level. Turning the endpoint volume
// down still reduces what reaches the converter on the great majority of
// devices, so it is the right control to reach for; it just may not have as
// much travel as the ALSA case.
class WasapiCaptureGain : public CaptureGainControl
{
public:
	WasapiCaptureGain() : m_vol(nullptr), m_uninitCom(false) {}

	~WasapiCaptureGain() override
	{
		if (m_vol)
			m_vol->Release();
		if (m_uninitCom)
			CoUninitialize();
	}

	bool open(const QString &device)
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		// S_FALSE means this thread was already in an apartment, and
		// RPC_E_CHANGED_MODE that it is in a different one - in both cases COM
		// is usable and we must not be the one to tear it down.
		if (SUCCEEDED(hr))
			m_uninitCom = true;
		else if (hr != RPC_E_CHANGED_MODE)
			return false;

		IMMDeviceEnumerator *devEnum = nullptr;
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
		                      __uuidof(IMMDeviceEnumerator), (void **)&devEnum);
		if (FAILED(hr) || !devEnum)
			return false;

		IMMDevice *dev = nullptr;
		if (device.isEmpty() || device == QLatin1String("default"))
		{
			// eCommunications to match what the capture stream itself opens in
			// WasapiBackend::resolveDevice() - otherwise on a machine where the
			// call endpoint and the console endpoint differ we would be turning
			// down a device nobody is recording from.
			hr = devEnum->GetDefaultAudioEndpoint(eCapture, eCommunications, &dev);
		}
		else
		{
			hr = devEnum->GetDevice((LPCWSTR)device.utf16(), &dev);
		}

		devEnum->Release();

		if (FAILED(hr) || !dev)
			return false;

		hr = dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&m_vol);
		dev->Release();

		if (FAILED(hr) || !m_vol)
		{
			qWarning() << "WASAPI: capture endpoint has no volume control:" << hrText(hr);
			m_vol = nullptr;
			return false;
		}

		m_device = device.isEmpty() ? QStringLiteral("default") : device;
		return true;
	}

	bool gain(double *out) override
	{
		if (!m_vol || !out)
			return false;

		float level = 0.0f;
		if (FAILED(m_vol->GetMasterVolumeLevelScalar(&level)))
			return false;

		*out = level;
		return true;
	}

	// WASAPI keeps a real decibel value alongside the scalar, so this needs no
	// conversion of our own.
	bool gainDb(double *out) override
	{
		if (!m_vol || !out)
			return false;

		float db = 0.0f;
		if (FAILED(m_vol->GetMasterVolumeLevel(&db)))
			return false;

		*out = db;
		return true;
	}

	bool setGain(double value) override
	{
		if (!m_vol)
			return false;

		float level = (float)(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
		return SUCCEEDED(m_vol->SetMasterVolumeLevelScalar(level, nullptr));
	}

	QString description() const override
	{
		return m_device + QStringLiteral(" (WASAPI endpoint volume)");
	}

private:
	IAudioEndpointVolume *m_vol;
	bool m_uninitCom;
	QString m_device;
};

CaptureGainControl *pica_create_wasapi_capture_gain(const QString &device)
{
	WasapiCaptureGain *g = new WasapiCaptureGain();

	if (g->open(device))
		return g;

	delete g;
	return nullptr;
}

#endif // Q_OS_WIN
