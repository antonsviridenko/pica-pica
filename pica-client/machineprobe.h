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
#ifndef MACHINEPROBE_H
#define MACHINEPROBE_H

#include "mediadevice.h"
#include "videodevice.h"
#include <QList>
#include <QMap>
#include <QString>
#include <functional>

class QObject;

// What this machine has to make calls with. Gathered once, off the GUI thread,
// and turned into settings by autoConfigure().
struct MachineProbe
{
	// Linux only: PulseAudio (or PipeWire's PulseAudio side) lists devices.
	bool pulseAnswers = false;
	// Under the audio system autoConfigure() will choose.
	QList<MediaDeviceInfo> audioCapture;
	QList<MediaDeviceInfo> audioPlayback;
	bool platformEchoCancellation = false;

	struct Camera
	{
		MediaDeviceInfo device;
		QList<VideoCaptureFormat> formats;
	};
	QList<Camera> cameras;

	// VAAPI. The Windows paths are left to the user: nothing here can tell
	// whether they work without trying them.
	bool hwEncode = false;
	bool hwDecode = false;

	int cpuThreads = 1;
};

// Blocking, a few tens of milliseconds on a desktop but as long as the devices
// take to answer. Briefly switches the process wide audio system setting to
// look for PulseAudio, so not for use during a call - see probeMachineAsync().
MachineProbe probeMachine();

// Runs probeMachine() on its own thread and hands the result to `done` on the
// GUI thread, unless `receiver` is gone by then. Does nothing, and returns
// false, while a call is in progress.
bool probeMachineAsync(QObject *receiver, std::function<void(const MachineProbe &)> done);

// The settings this machine should run with, as name -> value rows for the
// settings table. Only what the probe can decide: codecs, network settings and
// anything the probe found nothing for are left out, so the stored value or the
// built in default stays.
QMap<QString, QString> autoConfigure(const MachineProbe &p);

#endif // MACHINEPROBE_H
