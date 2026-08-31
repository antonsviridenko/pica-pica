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
#ifndef AUDIODEVICE_H
#define AUDIODEVICE_H

#include "mediadevice.h"
#include "echocanceller.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QAtomicInt>

class NativeAudioBackend;

// A single AudioDevice instance is dedicated to one direction - either
// capture+encode (via Capture(), fed from a local hardware input device) or
// decode+playback (via Play(), fed from the network via enqueuePacket()).
// Each instance is meant to live on its own QThread (see
// AudioVideoCallController), since both Capture() and Play() block the
// calling thread in a device I/O loop until Close() is called - the same
// pattern TonePlayer already uses for tone playback.
class AudioDevice : public QObject, public MediaDevice
{
	Q_OBJECT
public:
	AudioDevice(QObject *parent = nullptr);
	~AudioDevice();

	// Configure before invoking Capture()/Play(). deviceName is a platform
	// device identifier as accepted by the driver named by
	// PlatformDriverName() (e.g. an ALSA PCM name, a PulseAudio source name,
	// a WASAPI endpoint id, a CoreAudio device UID), or "default".
	Q_INVOKABLE void configureCapture(QString deviceName, QString codec, int sampleRate);
	Q_INVOKABLE void configurePlayback(QString deviceName, QString codec, int sampleRate);

	// Give this device the call's echo canceller. The capture direction runs
	// the microphone signal through it, the playback direction feeds it the
	// far end signal; both directions of one call must be given the same
	// object or there is nothing to cancel against.
	//
	// Ignored when the platform is doing its own cancellation - see
	// AudioVideoCallController::startAudioPipeline(), which is where that is
	// decided.
	void setEchoCanceller(EchoCancellerPtr ec);

	// Push a codec packet received from the network into the playback
	// jitter buffer. Safe to call from any thread. Drops the oldest queued
	// packet if the buffer is full, favoring low latency over completeness.
	//
	// seq_num is the packet's 0x76 sequence number. Over a mediac2c
	// connection packets can arrive out of order or twice, so the buffer is
	// ordered by it, and a packet that arrives after one further along the
	// stream has already been played is dropped rather than played out of
	// place.
	Q_INVOKABLE void enqueuePacket(quint16 seq_num, QByteArray data);

public slots:
	// Blocking capture+encode loop: opens the configured local input
	// device, encodes captured audio with the configured codec, and emits
	// packetReady() for each encoded packet. Returns once Close() is
	// called.
	virtual void Capture() override;

	// Blocking decode+playback loop: opens the configured local output
	// device, decodes packets pulled from the internal queue (see
	// enqueuePacket()) and writes the resulting audio to the device.
	// Returns once Close() is called.
	virtual void Play() override;

	// Signals a running Capture()/Play() loop to stop and return. Safe to
	// call from any thread, and safe to call even if nothing is running.
	virtual void Close() override;

signals:
	// One encoded packet produced by Capture(), ready to be sent over the
	// network.
	void packetReady(QByteArray data);

	void errorOccurred(QString message);

	// Emitted once a device is open, describing the format it settled on.
	// Counterpart of VideoDevice::accelerationInUse().
	void deviceFormatInUse(QString description);

public:
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection dir);

	// Name of the driver to use for one direction of audio.
	//
	// Two kinds of name come back. A bare name - "alsa", "pulse", "oss" - is
	// an FFmpeg libavdevice muxer/demuxer, opened through libavformat the way
	// the rest of this codebase opens things. A name starting with
	// "platform-" is not an FFmpeg driver at all: what follows the prefix
	// names a platform audio API that the caller has to drive itself through
	// NativeAudioBackend, because libavdevice cannot express what we need
	// from it.
	//
	// That is how Windows and macOS are handled. Both provide acoustic echo
	// cancellation, but only to streams that declare themselves part of a
	// voice call - AudioCategory_Communications on WASAPI, the
	// VoiceProcessingIO audio unit on CoreAudio - and libavdevice's
	// and "audiotoolbox" wrappers have no way to ask for either. Rather than
	// open the sound card the plain way and then cancel the echo ourselves on
	// platforms that would have done a better job of it below us, we talk to
	// those APIs directly.
	//
	// The direction matters because the answer can differ between them: it is
	// the capture side that Windows attaches its processing to, and on Linux
	// the two directions can sit on different servers.
	static QString PlatformDriverName(enum MediaDeviceStreamDirection dir);

	// True if the driver name is a "platform-<api>" one rather than an FFmpeg
	// driver, and the API name out of such a string.
	static bool IsPlatformDriver(const QString &driver);
	static QString PlatformApiName(const QString &driver);

	// The FFmpeg driver Linux should use, "alsa" or "pulse", from the
	// "audio.driver" setting. Meaningless on the other platforms, which have
	// exactly one audio API worth using.
	//
	// The value is cached in a process wide variable rather than read from
	// the settings database on demand, because Capture() and Play() run on
	// their own threads and a QSqlDatabase connection belongs to the thread
	// that opened it. Call the setter from the GUI thread at startup and
	// whenever the setting changes.
	static void SetLinuxDriverName(const QString &driver);
	static QString LinuxDriverName();

private:
	QString m_deviceName;
	QString m_codec;
	int m_sampleRate;

	QAtomicInt m_abort;

	// Shared with the other direction of the same call, and held by shared
	// pointer because AudioVideoCallController tears a call down without
	// joining either thread.
	EchoCancellerPtr m_echoCanceller;

	// The FFmpeg and the native halves of each direction. Which one runs is
	// decided by PlatformDriverName().
	void captureFFmpeg(const QString &driver);
	void captureNative(const QString &api);
	void playFFmpeg(const QString &driver);
	void playNative(const QString &api);

	// Jitter buffer for the playback direction: Play() blocks pulling from
	// here, enqueuePacket() (called from the controller on delivery of an
	// incoming network packet) pushes into it.
	QMutex m_queueMutex;
	QWaitCondition m_queueCond;

	struct PlaybackPacket
	{
		quint16 seq;
		QByteArray data;
	};

	// Kept ordered by sequence number, oldest first.
	QList<PlaybackPacket> m_playQueue;
	// Sequence number of the last packet handed to the decoder, once one has
	// been; anything not newer than it has missed its turn.
	quint16 m_lastPlayedSeq;
	bool m_havePlayedSeq;

	// Shared by both playback paths. waitForPreroll() blocks until the jitter
	// buffer has a cushion or the call ends; nextPacket() pops the next
	// packet, returning false once Close() has been called and nothing is
	// left. A true return with an empty *out is a spurious wakeup - carry on.
	void waitForPreroll();
	bool nextPacket(QByteArray *out);

	// Ceiling on the queue, in packets of 20ms each - one second. This is a
	// runaway guard, not a latency control: Play() pops as fast as it can and
	// the standing latency ends up in the sound card's buffer, not here, so
	// the queue only grows when the peer sends a burst or our playback thread
	// is not being scheduled. It has to be well clear of kPrerollDepth,
	// because overflow is handled by dropping the OLDEST packet
	// (enqueuePacket()) - that is, by throwing away audio that was about to
	// be played. At the previous value of 8 that was only 160ms, close enough
	// to the preroll that ordinary reordering over the direct UDP transport
	// could reach it and punch holes in the speech.
	static const int kMaxQueueDepth = 50;

	// Number of packets Play() waits to have buffered before writing the
	// first one, to build up a cushion against arrival jitter (packets
	// this call apart arrive roughly every 20ms - see
	// AudioVideoCallController::startAudioPipeline()).
	//
	// This is standing latency: whatever is prerolled here is audio the
	// listener hears that much later, for the whole call. 100ms buys a
	// useful margin over the 60ms this used to be while staying inside the
	// range where a conversation still feels immediate.
	static const int kPrerollDepth = 5;
};

#endif // AUDIODEVICE_H
