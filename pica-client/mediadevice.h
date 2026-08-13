#ifndef MEDIADEVICE_H
#define MEDIADEVICE_H

#include <QString>
#include <QStringList>
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
	// Compressed formats this device can deliver by itself, as FFmpeg codec
	// names, most preferred first. Filled in for cameras that offer such a
	// stream (see VideoDevice::CompressedFormats), empty otherwise.
	QStringList compressedFormats;
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
