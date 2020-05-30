#ifndef VIDEODEVICE_H
#define VIDEODEVICE_H

#include "mediadevice.h"

class VideoDevice : public MediaDevice
{
public:
	VideoDevice();
	~VideoDevice();
	virtual QList<MediaDeviceInfo> Enumerate();
};

#endif // VIDEODEVICE_H
