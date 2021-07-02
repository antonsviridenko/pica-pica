#include "videodevice.h"
#include <QFile>

#ifdef Q_OS_LINUX
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

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
			/* Skip metadata device nodes introduced in latest kernels
			 * See:
			 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=088ead25524583e2200aa99111bea2f66a86545a
			 * https://bugzilla.kernel.org/show_bug.cgi?id=199575
			 * Add only devices having V4L2_CAP_VIDEO_CAPTURE capability.
			*/
			int fd = ::open(d.device.toLatin1().constData(), O_RDWR | O_NONBLOCK);
			if (fd < 0)
				continue;
			struct v4l2_capability cap;
			memset(&cap, 0, sizeof cap);
			int ret  = ::ioctl(fd, VIDIOC_QUERYCAP, &cap);
			::close(fd);
			if (ret == -1)
				continue;
			if (!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE))
				continue;
			d.index = index++;

			result << d;
		}
	}
#endif
	return result;
}
