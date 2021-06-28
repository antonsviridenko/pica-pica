#ifndef VIDEODEVICE_H
#define VIDEODEVICE_H

#include "mediadevice.h"

class VideoDevice : public MediaDevice
{
public:
	VideoDevice();
	~VideoDevice();
	virtual QList<MediaDeviceInfo> Enumerate();
	void Capture();
	void Play();
	void Close();
};

#endif // VIDEODEVICE_H
