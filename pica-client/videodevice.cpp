#include "videodevice.h"
#include <QFile>

VideoDevice::VideoDevice()
{

}

VideoDevice::~VideoDevice()
{

}

void VideoDevice::Capture()
{
}

void VideoDevice::Play()
{
}

void VideoDevice::Close()
{
}

QList<MediaDeviceInfo> VideoDevice::Enumerate()
{
	QList<MediaDeviceInfo> result;
	int index = 0;
#ifdef Q_OS_LINUX
	for (int i = 0; i < 64; i++)
	{
		QString device = QString(QLatin1String("/dev/video%1")).arg(i);
		if (QFile::exists(device))
		{
			MediaDeviceInfo d;
			d.device = device;

			QFile v4l2_name(QString(QLatin1String("/sys/class/video4linux/video%1/name")).arg(i));
			if (v4l2_name.open((QIODevice::ReadOnly | QIODevice::Text)))
			{
				d.humanReadable = QString(v4l2_name.readLine()).trimmed();
				v4l2_name.close();
			}
			d.index = index++;

			result << d;
		}
	}
#endif
	return result;
}
