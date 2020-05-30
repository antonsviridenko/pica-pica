#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include "mediadevice.h"

class AudioDevice : public MediaDevice
{
public:
	AudioDevice();
	~AudioDevice();
	virtual QList<MediaDeviceInfo> Enumerate();
};

#endif // AUDIODEVICE_H
