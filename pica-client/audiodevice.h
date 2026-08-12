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
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QAtomicInt>

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
	// device identifier as accepted by the FFmpeg driver named by
	// PlatformDriverName() (e.g. an ALSA PCM name), or "default".
	Q_INVOKABLE void configureCapture(QString deviceName, QString codec, int sampleRate);
	Q_INVOKABLE void configurePlayback(QString deviceName, QString codec, int sampleRate);

	// Push a codec packet received from the network into the playback
	// jitter buffer. Safe to call from any thread. Drops the oldest queued
	// packet if the buffer is full, favoring low latency over completeness.
	Q_INVOKABLE void enqueuePacket(QByteArray data);

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

public:
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection dir);

	// Name of the FFmpeg libavdevice muxer/demuxer for the host platform's
	// native audio API (e.g. "alsa" on Linux, "audiotoolbox" on macOS,
	// "dsound" on Windows). Shared by every component that opens an audio
	// device through FFmpeg (capture, playback, TonePlayer).
	static QString PlatformDriverName();

private:
	QString m_deviceName;
	QString m_codec;
	int m_sampleRate;

	QAtomicInt m_abort;

	// Jitter buffer for the playback direction: Play() blocks pulling from
	// here, enqueuePacket() (called from the controller on delivery of an
	// incoming network packet) pushes into it.
	QMutex m_queueMutex;
	QWaitCondition m_queueCond;
	QQueue<QByteArray> m_playQueue;
	static const int kMaxQueueDepth = 8;

	// Number of packets Play() waits to have buffered before writing the
	// first one, to build up a cushion against arrival jitter (packets
	// this call apart arrive roughly every 20ms - see
	// AudioVideoCallController::startAudioPipeline()).
	static const int kPrerollDepth = 3;
};

#endif // AUDIODEVICE_H
