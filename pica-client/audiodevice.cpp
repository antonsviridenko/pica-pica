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
