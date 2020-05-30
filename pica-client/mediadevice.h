#ifndef MEDIADEVICE_H
#define MEDIADEVICE_H

#include <QString>
#include <QList>

struct MediaDeviceInfo
{
	QString device;
	QString humanReadable;
	int index;
};

class MediaDevice
{
public:
	MediaDevice();
	virtual ~MediaDevice();
	virtual QList<MediaDeviceInfo> Enumerate() = 0;
};

#endif // MEDIADEVICE_H
