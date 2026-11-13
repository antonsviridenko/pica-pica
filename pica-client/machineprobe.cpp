/*
	(c) Copyright  2012 - 2026 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "machineprobe.h"
#include "globals.h"
#include "audiodevice.h"
#include "audiovideocallcontroller.h"
#include "callsettings.h"
#include <QCoreApplication>
#include <QPointer>
#include <QThread>

MachineProbe probeMachine()
{
	MachineProbe p;

	p.cpuThreads = QThread::idealThreadCount();

#ifdef Q_OS_LINUX
	// Enumerate() lists the devices of whichever audio system is selected, and
	// that selection is process wide - so look at PulseAudio through it and put
	// it back if there is nothing there.
	const QString driverBefore = AudioDevice::LinuxDriverName();

	AudioDevice::SetLinuxDriverName(QStringLiteral("pulse"));
	p.pulseAnswers = AudioDevice::LinuxDriverName() == QLatin1String("pulse")
	                 && !AudioDevice().Enumerate(PLAYBACK).isEmpty();

	if (!p.pulseAnswers)
		AudioDevice::SetLinuxDriverName(QStringLiteral("alsa"));
#endif

	p.audioCapture = AudioDevice().Enumerate(CAPTURE);
	p.audioPlayback = AudioDevice().Enumerate(PLAYBACK);
	p.platformEchoCancellation = AudioDevice::PlatformEchoCancellationAvailable(AudioDevice::PlatformDriverName(CAPTURE));

#ifdef Q_OS_LINUX
	AudioDevice::SetLinuxDriverName(driverBefore);
#endif

	QList<MediaDeviceInfo> cams = VideoDevice().Enumerate(CAPTURE);

	for (int i = 0; i < cams.size(); i++)
	{
		MachineProbe::Camera c;

		c.device = cams[i];
		c.formats = VideoDevice::CaptureFormats(cams[i].device);
		p.cameras << c;
	}

#ifdef HAVE_VAAPI
	if (VaapiContext::isAvailable())
	{
		p.hwEncode = VideoDevice::AccelerationBuiltIn(VideoAccelerationVaapi, true);
		p.hwDecode = VideoDevice::AccelerationBuiltIn(VideoAccelerationVaapi, false);
	}
#endif

	return p;
}

bool probeMachineAsync(QObject *receiver, std::function<void(const MachineProbe &)> done)
{
	// the probe moves the audio system setting a running call reads when it opens a device
	if (avctrl && avctrl->isActive())
		return false;

	QPointer<QObject> guard(receiver);

	QThread *t = QThread::create([guard, done]
	{
		const MachineProbe p = probeMachine();

		QMetaObject::invokeMethod(qApp, [guard, done, p]
		{
			if (guard)
				done(p);
		}, Qt::QueuedConnection);
	});

	QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
	t->start();

	return true;
}

static QString pickDevice(const QList<MediaDeviceInfo> &devs)
{
	for (int i = 0; i < devs.size(); i++)
		if (devs[i].device == QLatin1String("default"))
			return devs[i].device;

	return devs.isEmpty() ? QString() : devs.first().device;
}

// The highest rate on offer that does not exceed `limit`, or the lowest one
// when they all do. Zero when the camera did not say what it offers.
static int pickRate(const QList<int> &rates, int limit)
{
	int best = 0, lowest = 0;

	for (int i = 0; i < rates.size(); i++)
	{
		if (rates[i] <= limit && rates[i] > best)
			best = rates[i];
		if (lowest == 0 || rates[i] < lowest)
			lowest = rates[i];
	}

	return best ? best : lowest;
}

QMap<QString, QString> autoConfigure(const MachineProbe &p)
{
	QMap<QString, QString> v;

#ifdef Q_OS_LINUX
	v["audio.driver"] = p.pulseAnswers ? "pulse" : "alsa";
#endif

	if (!p.audioCapture.isEmpty())
		v["audio.capture_device"] = pickDevice(p.audioCapture);

	if (!p.audioPlayback.isEmpty())
	{
		v["audio.playback_device"] = pickDevice(p.audioPlayback);
		v["audio.ring_device"] = pickDevice(p.audioPlayback);
	}

	// the same preference loadEchoCancellationSetting() has when nothing is stored
	v["audio.echo_cancellation"] = p.platformEchoCancellation ? "platform" : "own";

#ifdef HAVE_VAAPI
	// both fall back to software on their own when the driver turns out not to manage
	v["video.vaapi_encoding"] = p.hwEncode ? "1" : "0";
	v["video.vaapi_decoding"] = p.hwDecode ? "1" : "0";
#endif

	if (p.cameras.isEmpty())
		return v;

	// Encoding in software on a machine with two threads or fewer has to share
	// them with the echo canceller and the decoder of the other direction.
	const bool lowEnd = p.cpuThreads <= 2 && !p.hwEncode;
	const QSize limit = lowEnd ? QSize(640, 480) : QSize(1280, 720);
	const int rateLimit = lowEnd ? 15 : 30;
	const int smoothRate = lowEnd ? 10 : 24;

	const MachineProbe::Camera &cam = p.cameras.first();
	int width = 0, height = 0, rate = 0;
	bool smooth = false;

	v["video.capture_device"] = cam.device.device;

	// The largest size within the limit - but a size that reaches a smooth frame
	// rate beats a larger one that does not.
	for (int i = 0; i < cam.formats.size(); i++)
	{
		const VideoCaptureFormat &f = cam.formats[i];
		const int r = pickRate(f.frameRates, rateLimit);
		const bool s = r == 0 || r >= smoothRate; // a camera that names no rates gets the benefit of the doubt

		if (f.width > limit.width() || f.height > limit.height())
			continue;

		if ((s && !smooth) || (s == smooth && f.width * f.height > width * height))
		{
			width = f.width;
			height = f.height;
			rate = r;
			smooth = s;
		}
	}

	// nothing listed, or nothing small enough: the built in defaults stay
	if (width == 0)
		return v;

	v["video.capture_width"] = QString::number(width);
	v["video.capture_height"] = QString::number(height);
	v["video.capture_framerate"] = QString::number(rate ? rate : qMin(rateLimit, (int)kDefaultCaptureFrameRate));
	v["call.video_bitrate_kbps"] = QString::number(height >= 720 ? 1500 : height >= 480 ? 800 : 400);

	return v;
}
