/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
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
#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include "mediadevice.h"

class AudioDevice : public MediaDevice
{
public:
	AudioDevice();
	~AudioDevice();
	virtual void Capture();
	virtual void Play();
	virtual void Close();
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection dir);

	// Name of the FFmpeg libavdevice muxer/demuxer for the host platform's
	// native audio API (e.g. "alsa" on Linux, "audiotoolbox" on macOS,
	// "dsound" on Windows). Shared by every component that opens an audio
	// device through FFmpeg (capture, playback, TonePlayer).
	static QString PlatformDriverName();
};

#endif // AUDIODEVICE_H
