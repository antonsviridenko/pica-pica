#ifndef MEDIADEVICE_H
#define MEDIADEVICE_H

#include <QString>
#include <QList>

enum MediaDeviceStreamDirection
{
	CAPTURE,
	PLAYBACK
};

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
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection) = 0;
	virtual void Capture() = 0;
	virtual void Play() = 0;
	virtual void Close() = 0;
};

#endif // MEDIADEVICE_H
