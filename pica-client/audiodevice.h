#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include "mediadevice.h"

class AudioDevice : public MediaDevice
{
public:
	AudioDevice();
	~AudioDevice();
	virtual void Capture();
	virtual void Play();
	virtual void Close();
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection dir);
};

#endif // AUDIODEVICE_H
