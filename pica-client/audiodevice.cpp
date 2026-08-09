/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
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
#include "audiodevice.h"

#ifdef Q_OS_LINUX
#include <alsa/asoundlib.h>
#endif

AudioDevice::AudioDevice()
{

}

AudioDevice::~AudioDevice()
{

}

void AudioDevice::Capture()
{
}

void AudioDevice::Play()
{
}

void AudioDevice::Close()
{
}

QList<MediaDeviceInfo> AudioDevice::Enumerate(enum MediaDeviceStreamDirection dir)
{
	QList<MediaDeviceInfo> result;
	int index = 0;

#ifdef Q_OS_LINUX
	void **hints, **n;
	char *name = NULL, *descr = NULL, *io = NULL;
	const char *direction = (dir == PLAYBACK) ? "Output" : "Input";

	if (snd_device_name_hint(-1, "pcm", &hints) < 0)
		return result;
	n = hints;
	while(*n)
	{
		MediaDeviceInfo d;

		name = snd_device_name_get_hint(*n, "NAME");
		descr = snd_device_name_get_hint(*n, "DESC");
		io = snd_device_name_get_hint(*n, "IOID");

		if (io && strcmp(io, direction))
			goto release_resources;

		d.device = QLatin1String(name);
		d.humanReadable = QLatin1String(descr);
		d.index = index++;
		if (strstr(name, "default") == name)
			result.prepend(d);
		else
			result << d;

	release_resources:
		free(io);
		free(descr);
		free(name);
		n++;
	}
	snd_device_name_free_hint(hints);
#endif
	return result;
}

QString AudioDevice::PlatformDriverName()
{
#if defined(Q_OS_LINUX)
	return QStringLiteral("alsa");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	return QStringLiteral("audiotoolbox");
#elif defined(Q_OS_WIN)
	return QStringLiteral("dsound");
#else
	return QStringLiteral("oss");
#endif
}
