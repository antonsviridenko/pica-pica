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

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

#include <QDebug>
#include <QVector>

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

// Small local copies of the device lookup helpers in
// nativeaudio_coreaudio.cpp. They are static over there and this is the only
// other place that needs them; duplicating three short functions beats
// exporting an internal API out of that file.
static QVector<AudioDeviceID> allInputCapableDevices()
{
	QVector<AudioDeviceID> devices;

	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioHardwarePropertyDevices;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMaster;

	UInt32 size = 0;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr)
		return devices;

	devices.resize(size / sizeof(AudioDeviceID));
	if (devices.isEmpty())
		return devices;

	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size,
	                               devices.data()) != noErr)
		devices.clear();

	return devices;
}

static QString deviceUidOf(AudioDeviceID dev)
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyDeviceUID;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMaster;

	CFStringRef uid = nullptr;
	UInt32 size = sizeof(uid);
	if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &uid) != noErr || !uid)
		return QString();

	char buf[512] = {0};
	QString result;
	if (CFStringGetCString(uid, buf, sizeof(buf), kCFStringEncodingUTF8))
		result = QString::fromUtf8(buf);

	CFRelease(uid);
	return result;
}

static AudioDeviceID defaultInputDevice()
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMaster;

	AudioDeviceID dev = kAudioObjectUnknown;
	UInt32 size = sizeof(dev);
	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &dev) != noErr)
		return kAudioObjectUnknown;

	return dev;
}

// The input volume of a CoreAudio device.
//
// Which element carries it varies: some devices expose a master (element 0),
// some only per channel controls, and plenty of built in microphones expose
// none at all and are simply not adjustable. open() works that out once and
// remembers the answer; a device with nothing settable fails to open, and the
// caller falls back to warning the user.
class CoreAudioCaptureGain : public CaptureGainControl
{
public:
	CoreAudioCaptureGain() : m_dev(kAudioObjectUnknown), m_master(false) {}

	bool open(const QString &device)
	{
		if (device.isEmpty() || device == QLatin1String("default"))
		{
			m_dev = defaultInputDevice();
		}
		else
		{
			QVector<AudioDeviceID> devices = allInputCapableDevices();
			for (int i = 0; i < devices.size(); i++)
			{
				if (deviceUidOf(devices[i]) == device)
				{
					m_dev = devices[i];
					break;
				}
			}

			// Fall back rather than give up - the same thing the audio unit
			// does when a remembered device has been unplugged.
			if (m_dev == kAudioObjectUnknown)
				m_dev = defaultInputDevice();
		}

		if (m_dev == kAudioObjectUnknown)
			return false;

		if (settable(0))
		{
			m_master = true;
			m_channels.clear();
		}
		else
		{
			// Stereo inputs are the common case; anything past the second
			// channel is rare enough on a microphone not to chase.
			for (UInt32 ch = 1; ch <= 2; ch++)
			{
				if (settable(ch))
					m_channels << ch;
			}

			if (m_channels.isEmpty())
			{
				qWarning() << "CoreAudio: input device has no adjustable volume";
				return false;
			}
		}

		m_device = device.isEmpty() ? QStringLiteral("default") : device;
		return true;
	}

	bool gain(double *out) override
	{
		if (!out || m_dev == kAudioObjectUnknown)
			return false;

		// The loudest of the channels, so stepping down moves all of them.
		Float32 best = -1.0f;

		if (m_master)
		{
			if (!readElement(0, &best))
				return false;
		}
		else
		{
			for (int i = 0; i < m_channels.size(); i++)
			{
				Float32 v = 0.0f;
				if (readElement(m_channels[i], &v) && v > best)
					best = v;
			}
		}

		if (best < 0.0f)
			return false;

		*out = best;
		return true;
	}

	// kAudioDevicePropertyVolumeDecibels is the same control as the scalar
	// above, reported on a decibel scale. Devices that expose the scalar do not
	// always expose this one, hence the plain false rather than a fallback.
	bool gainDb(double *out) override
	{
		if (!out || m_dev == kAudioObjectUnknown)
			return false;

		AudioObjectPropertyAddress addr;
		addr.mSelector = kAudioDevicePropertyVolumeDecibels;
		addr.mScope = kAudioObjectPropertyScopeInput;
		addr.mElement = m_master ? 0 : (m_channels.isEmpty() ? 0 : m_channels[0]);

		if (!AudioObjectHasProperty(m_dev, &addr))
			return false;

		Float32 db = 0.0f;
		UInt32 size = sizeof(db);
		if (AudioObjectGetPropertyData(m_dev, &addr, 0, nullptr, &size, &db) != noErr)
			return false;

		*out = db;
		return true;
	}

	bool setGain(double value) override
	{
		if (m_dev == kAudioObjectUnknown)
			return false;

		Float32 v = (Float32)(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));

		if (m_master)
			return writeElement(0, v);

		bool any = false;
		for (int i = 0; i < m_channels.size(); i++)
		{
			if (writeElement(m_channels[i], v))
				any = true;
		}

		return any;
	}

	QString description() const override
	{
		return m_device + QStringLiteral(" (CoreAudio input volume)");
	}

private:
	static AudioObjectPropertyAddress addressFor(UInt32 element)
	{
		AudioObjectPropertyAddress addr;
		addr.mSelector = kAudioDevicePropertyVolumeScalar;
		addr.mScope = kAudioObjectPropertyScopeInput;
		addr.mElement = element;
		return addr;
	}

	bool settable(UInt32 element)
	{
		AudioObjectPropertyAddress addr = addressFor(element);

		if (!AudioObjectHasProperty(m_dev, &addr))
			return false;

		Boolean writable = false;
		if (AudioObjectIsPropertySettable(m_dev, &addr, &writable) != noErr)
			return false;

		return writable;
	}

	bool readElement(UInt32 element, Float32 *out)
	{
		AudioObjectPropertyAddress addr = addressFor(element);
		UInt32 size = sizeof(*out);
		return AudioObjectGetPropertyData(m_dev, &addr, 0, nullptr, &size, out) == noErr;
	}

	bool writeElement(UInt32 element, Float32 value)
	{
		AudioObjectPropertyAddress addr = addressFor(element);
		return AudioObjectSetPropertyData(m_dev, &addr, 0, nullptr, sizeof(value), &value) == noErr;
	}

	AudioDeviceID m_dev;
	bool m_master;
	QVector<UInt32> m_channels;
	QString m_device;
};

CaptureGainControl *pica_create_coreaudio_capture_gain(const QString &device)
{
	CoreAudioCaptureGain *g = new CoreAudioCaptureGain();

	if (g->open(device))
		return g;

	delete g;
	return nullptr;
}

#endif // Q_OS_MACOS
