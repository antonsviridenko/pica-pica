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
#ifndef VIDEODEVICE_H
#define VIDEODEVICE_H

#include "mediadevice.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QAtomicInt>

// Reassembles the fragments of one encoded video frame, as carried by 0x77
// protocol messages - see the fragmentation description in
// doc/proto-doc-latest.txt. Used both on the receiving end of a call and by
// the settings dialog's local pipeline test.
class VideoFrameAssembler
{
public:
	// A 0x77 sequence number is a last fragment marker in its top bit plus a
	// 15 bit packet counter.
	static const quint16 LastFragmentFlag = 0x8000;
	static const quint16 SeqNumMask = 0x7FFF;

	VideoFrameAssembler();

	// Adds one received fragment. Returns the complete encoded frame once
	// the fragment carrying the last fragment marker has been added, and an
	// empty QByteArray while the frame is still incomplete.
	QByteArray addFragment(quint16 seq_num, quint32 timestamp, const QByteArray &data);

	void reset();

private:
	QByteArray m_buffer;
	quint32 m_timestamp;
	bool m_inProgress;
};

// Video counterpart of AudioDevice: a single instance is dedicated to one
// direction - either capture+encode (Capture(), reading from a local camera)
// or decode (Play(), fed from the network via enqueueFrame()). Each instance
// is meant to live on its own QThread, since both Capture() and Play() block
// the calling thread in a loop until Close() is called.
//
// Note the calling rules that come with that, the same ones AudioDevice
// follows: enqueueFrame() and Close() have to reach a loop that is already
// running, so they are internally thread-safe and must be called directly
// rather than through QMetaObject::invokeMethod() - a queued call would sit
// in the event queue behind the very loop it is meant to feed or interrupt.
// configureCapture()/configurePlayback() are the exception: they only run
// before the loop starts, while the thread is still idle.
class VideoDevice : public QObject, public MediaDevice
{
	Q_OBJECT
public:
	VideoDevice(QObject *parent = nullptr);
	~VideoDevice();

	Q_INVOKABLE void configureCapture(QString deviceName, QString codec, int width, int height);
	Q_INVOKABLE void configurePlayback(QString codec, int width, int height);

	// Push one complete encoded frame (already reassembled from 0x77
	// fragments by the caller) into the decode queue. Safe to call from any
	// thread. Drops the oldest queued frame if the buffer is full - for live
	// video, showing the newest frame matters more than showing every frame.
	void enqueueFrame(QByteArray encodedFrame);

public slots:
	// Blocking capture+encode loop: opens the configured camera, encodes
	// captured frames, and emits packetReady() for each encoded frame, split
	// into fragments that fit a single 0x77 protocol message. Returns once
	// Close() is called.
	virtual void Capture() override;

	// Blocking decode loop: decodes frames pulled from the internal queue
	// (see enqueueFrame()) and emits frameReady() for each decoded picture.
	// Returns once Close() is called.
	virtual void Play() override;

	// Signals a running Capture()/Play() loop to stop and return. Safe to
	// call from any thread, and safe to call even if nothing is running.
	virtual void Close() override;

signals:
	// One fragment of an encoded frame, ready to be sent over the network.
	// isLastFragment marks the final fragment of a frame - it maps directly
	// onto the 0x77 sequence number's last fragment marker bit.
	void packetReady(QByteArray data, bool isLastFragment);

	// One decoded picture from the remote side, ready to be displayed.
	void frameReady(QImage frame);

	void errorOccurred(QString message);

public:
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection dir);

	// Name of the FFmpeg libavdevice demuxer for the host platform's native
	// video capture API. Counterpart of AudioDevice::PlatformDriverName().
	static QString PlatformDriverName();

private:
	QString m_deviceName;
	QString m_codec;
	int m_width;
	int m_height;

	QAtomicInt m_abort;

	QMutex m_queueMutex;
	QWaitCondition m_queueCond;
	QQueue<QByteArray> m_frameQueue;
	static const int kMaxQueueDepth = 2;
};

#endif // VIDEODEVICE_H
