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
#ifndef NATIVEAUDIO_H
#define NATIVEAUDIO_H

#include "mediadevice.h"
#include <QString>
#include <QList>

// Audio input/output through a platform API directly, bypassing FFmpeg's
// libavdevice.
//
// FFmpeg's audio device wrappers open the sound card the plain way, which on
// Windows and macOS means no acoustic echo cancellation: both platforms only
// run their own AEC for streams that were opened declaring themselves part of
// a voice call, and libavdevice has no way to say that. Since that AEC is
// better than anything we can do in process - it sits below us in the audio
// stack and can see the real speaker signal and the real clock - it is worth
// talking to those APIs ourselves.
//
// AudioDevice picks between this and the FFmpeg path on the strength of the
// driver string PlatformDriverName() returns: a name prefixed with
// "platform-" means the rest of it is an apiName() below.
//
// One instance drives one direction. Whether the two directions of a call end
// up sharing anything underneath is the backend's business - on macOS they
// have to, because the AEC there lives in a single audio unit that renders
// and captures at once.

// What a stream is going to be used for.
//
// A call stream asks the platform for its voice processing, echo cancellation
// above all. A plain one does not, and must not: on macOS the echo canceller
// is an audio unit that captures as well as renders, so asking for it to play
// a ringtone would open the microphone, prompt for permission and light the
// recording indicator, all to make a telephone noise.
enum NativeAudioMode
{
	NativeAudioVoiceCall,
	NativeAudioPlain
};

class NativeAudioBackend
{
public:
	virtual ~NativeAudioBackend();

	// The platform API this backend speaks, matching what follows the
	// "platform-" prefix in a driver name.
	virtual QString apiName() const = 0;

	// device is one of the MediaDeviceInfo::device strings enumerate()
	// returned, or "default". On failure *error, if given, is filled in with
	// something worth showing the user.
	virtual bool openCapture(const QString &device, int sampleRate, int channels,
	                         NativeAudioMode mode, QString *error) = 0;
	virtual bool openPlayback(const QString &device, int sampleRate, int channels,
	                          NativeAudioMode mode, QString *error) = 0;

	// Reads up to maxSamples interleaved 16 bit samples, waiting at most
	// timeoutMs for the first of them. Returns the number of samples read,
	// which may be zero if the timeout ran out, or negative on error.
	virtual int read(qint16 *buf, int maxSamples, int timeoutMs) = 0;

	// Hands nsamples interleaved 16 bit samples to the device, waiting up to
	// timeoutMs for room. Returns the number accepted - short writes happen
	// when the timeout expires with the device buffer still full - or
	// negative on error.
	virtual int write(const qint16 *buf, int nsamples, int timeoutMs) = 0;

	virtual void close() = 0;

	// One line describing what the stream actually negotiated, for the
	// settings dialog to show next to the test result. What a device really
	// gave us, as opposed to what we asked it for, is otherwise invisible
	// from outside - and it is the first thing worth knowing when a recording
	// comes out wrong. Empty when a backend has nothing to add.
	virtual QString formatDescription() const { return QString(); }

	// Whether the stream was opened in a mode the platform documents as
	// running its own acoustic echo cancellation, in which case the caller
	// should not add ours on top. Note this reports what we asked for and
	// were granted, which on Windows is not the same as a promise that the
	// endpoint's driver actually implements the effect.
	virtual bool hasPlatformEchoCancellation() const = 0;

	// True if this build knows the named API at all.
	static bool isAvailable(const QString &api);

	// Caller owns the result. Returns null for an API this build has no
	// implementation of.
	static NativeAudioBackend *create(const QString &api);

	static QList<MediaDeviceInfo> enumerate(const QString &api, enum MediaDeviceStreamDirection dir);
};

#endif // NATIVEAUDIO_H
