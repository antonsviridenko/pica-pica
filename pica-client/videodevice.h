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
#include "callsettings.h"
#include "vaapi.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QSize>
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
	// empty QByteArray while the frame is still incomplete or while waiting
	// to resynchronize after a loss.
	QByteArray addFragment(quint16 seq_num, quint32 timestamp, const QByteArray &data);

	void reset();

private:
	QByteArray m_buffer;
	quint32 m_timestamp;
	bool m_inProgress;
	// Counter part of the sequence number of the last accepted fragment, and
	// whether that fragment ended a frame. Together they say whether the
	// fragment being added continues what came before it or whether
	// something was lost on the way - see addFragment().
	quint16 m_lastSeq;
	bool m_lastWasFrameEnd;
	bool m_haveLastSeq;
};

// One pixel format a camera can deliver a particular size in, with the frame
// rates it can deliver that combination at.
//
// The rates are kept per format rather than merged across them because they
// genuinely differ, by a lot. Uncompressed video at 30fps needs more bandwidth
// than USB 2.0 has for anything much above VGA, so a camera that manages
// 1280x720 at 30fps in MJPEG manages only 10fps uncompressed, and 1920x1080 at
// 5fps. A camera asked for more than a format can carry does not refuse: it
// reports success and quietly sends fewer frames. Keeping this association is
// what lets VideoDevice::Capture() ask for a format that can actually deliver
// what was requested instead of finding out afterwards.
struct VideoPixelFormat
{
	// FFmpeg codec name - "mjpeg", "h264", "hevc" - spelled as the v4l2
	// demuxer's input_format option and the 0x75 message spell it. Empty for
	// an uncompressed format, which is what a camera gives when asked for
	// nothing in particular.
	QString codec;

	// Frame rates available at this size in this format, in whole frames per
	// second, highest first. Empty when the camera would not say - which is
	// not the same as "none", see VideoDevice::Capture().
	QList<int> frameRates;
};

// One picture size a camera can deliver, and what it can deliver it in.
// Reported by VideoDevice::CaptureFormats() and shown by the settings dialog's
// resolution and frame rate lists.
struct VideoCaptureFormat
{
	int width;
	int height;

	// Every pixel format this size is available in, uncompressed first and
	// then the compressed ones in the same order as compressedFormats below.
	// This is the detail the two lists after it flatten away.
	QList<VideoPixelFormat> pixelFormats;

	// Frame rates the camera offers at this size in its best format for the
	// purpose - the union of the rates in pixelFormats. What the size can
	// manage at all, rather than what any one format can: reaching the top of
	// this list may mean capturing compressed and re-encoding, which is
	// Capture()'s business and not the caller's. Highest first, and empty when
	// the camera would not say.
	QList<int> frameRates;

	// Compressed formats available at this size, as FFmpeg codec names, most
	// preferred first. The same names and the same order as
	// CompressedFormats() reports for the whole camera, but per size - a
	// camera routinely offers MJPEG at its larger sizes only, which is exactly
	// what the "prefer compressed formats" setting turns on and what makes the
	// choice of size worth showing alongside.
	QStringList compressedFormats;
};

// Records that a size is available in `codec` - empty for uncompressed - at
// `rates`, adding to that format's entry rather than replacing it, since two
// of a platform's pixel formats can map to the same codec name. The rates go
// into the size's own union as well.
//
// Shared by the v4l2 and the DirectShow enumerator so that both build the same
// shape out of quite different source data.
void pica_merge_capture_rates(VideoCaptureFormat &entry, const QString &codec,
                              const QList<int> &rates);

// Puts one size's formats into the order VideoCaptureFormat documents and sorts
// every rate list highest first. Call once per size, after all of its formats
// have been merged in and compressedFormats has been put in the platform's
// preference order - that order is what pixelFormats is then arranged to match.
void pica_finish_capture_format(VideoCaptureFormat &entry);

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

	// width, height and frameRate are what the camera is asked for; a camera
	// that will not honour them is opened with them loosened rather than not
	// at all, see Capture().
	//
	// preferCompressed asks for the camera's own compressed stream to be
	// forwarded untouched when it offers one, saving the cost of decoding and
	// re-encoding every frame. The codec that ends up being used is only
	// known once the camera has been opened, and is reported by
	// captureStarted() - it is not necessarily the one requested here.
	//
	// codec is the FFmpeg name of the codec to encode to - "h264", "hevc" or
	// "vp9" - and bitrate the rate to encode at, in bits per second. Both
	// apply to frames this class encodes itself; a camera stream being
	// forwarded untouched is already encoded and neither has anything to act
	// on. codec is preferred over the camera's other compressed formats when
	// looking for one to forward, so choosing a codec the camera can produce
	// itself gets that one rather than merely a compatible one.
	//
	// acceleration is a set of VideoAcceleration flags naming the hardware
	// encoders that may be tried, in this order when several are set: VAAPI,
	// Direct3D 12, Media Foundation. Each is a request, not a guarantee: one not
	// built in, without usable hardware, or that will not open is passed over
	// for the next, and with none left capture encodes in software. Flags for
	// decoding-only paths are ignored.
	Q_INVOKABLE void configureCapture(QString deviceName, int width, int height, int frameRate,
	                                  bool preferCompressed, QString codec, int bitrate,
	                                  int acceleration);

	// acceleration is the decoding counterpart of configureCapture()'s: the
	// VideoAcceleration flags for the GPU decoders that may be tried, in this
	// order: VAAPI, Direct3D 11, Direct3D 12, DXVA2. The first whose device
	// comes up is the one used - whether the GPU can decode the particular
	// stream is only found out from the stream itself, and a GPU that cannot
	// leaves decoding in software rather than moving on to the next API.
	//
	// useVaapiRender additionally keeps the decoded frames on the GPU and emits
	// them through hwFrameReady() instead of frameReady(), for a renderer that
	// can draw a VA surface directly; it implies VAAPI decoding, since there is
	// no surface to draw otherwise. The Windows decoders always bring their
	// frames back to system memory - nothing in the client can draw a Direct3D
	// surface yet.
	Q_INVOKABLE void configurePlayback(QString codec, int width, int height,
	                                   int acceleration, bool useVaapiRender);

	// Push one complete encoded frame (already reassembled from 0x77
	// fragments by the caller) into the decode queue. Safe to call from any
	// thread. Drops the oldest queued frame if the buffer is full - for live
	// video, showing the newest frame matters more than showing every frame.
	void enqueueFrame(QByteArray encodedFrame);

	// Largest amount of encoded data one fragment emitted by packetReady()
	// may carry. It changes while a call is running: a media packet sent over
	// a mediac2c connection has to fit into a single datagram, which is far
	// less than a 0x77 message can hold over TCP. Safe to call from any
	// thread, and called directly rather than queued for the same reason
	// enqueueFrame() is - the capture loop it affects is already running.
	void setMaxFragmentSize(int size);

	// Stops keeping decoded frames in GPU memory, for when the renderer that
	// was to draw them turns out to not be able to: decoding carries on, but
	// frames come back through system memory and are emitted by frameReady()
	// from then on. Safe to call from any thread, and called directly rather
	// than queued for the same reason enqueueFrame() is.
	void disableHardwareRendering();

	// The size of the area the decoded frames are going to be drawn in, so
	// that Play() can produce them at that size directly. A decoded frame has
	// to be resized to the window either way, and doing it inside the colour
	// conversion the decode loop already runs costs far less than a second
	// pass over the picture afterwards - between 1.1 and 4.6 times less over
	// the whole pipeline, see tests/test_videoscaler.
	//
	// Frames keep their aspect ratio: this is the area to fit inside, not the
	// size to produce. An empty size means the display size is not known and
	// frames come back at their own resolution.
	//
	// Changes while a call is running, on every window resize, and is safe to
	// call from any thread. Called directly rather than queued for the same
	// reason enqueueFrame() is - the decode loop it affects is already
	// running.
	void setDisplaySize(QSize size);

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
	// Emitted by Capture() once the camera is open and the format actually
	// being sent is known - either the camera's own compressed format, when
	// forwarding it untouched, or the one this class encodes to. Carries the
	// values to announce to the peer, before any packet is emitted.
	//
	// frameRate is what the camera settled on, which is not necessarily what
	// configureCapture() asked for: a camera asked for a rate a pixel format
	// cannot carry opens anyway and then delivers fewer frames. Fractional
	// because cameras really do offer rates like 7.5fps. Falls back to the
	// requested rate when the camera would not say what it is doing, so a
	// shortfall it kept quiet about shows up in the measured rate instead -
	// see the settings dialog's fps counter.
	void captureStarted(QString codec, int width, int height, double frameRate);

	// One fragment of an encoded frame, ready to be sent over the network.
	// isLastFragment marks the final fragment of a frame - it maps directly
	// onto the 0x77 sequence number's last fragment marker bit.
	void packetReady(QByteArray data, bool isLastFragment);

	// One decoded picture from the remote side, ready to be displayed, as a
	// Format_RGB32 image - the format a widget draws without converting it
	// first.
	//
	// Already scaled to fit the area last given to setDisplaySize(), keeping
	// its aspect ratio, so the receiver can draw it as it stands. At the
	// frame's own resolution while no display size has been set. A receiver
	// still has to cope with the occasional frame at another size: the ones
	// already decoded or in flight when the window is resized arrive at the
	// size that was current when they were converted.
	void frameReady(QImage frame);

#ifdef HAVE_VAAPI
	// One decoded picture still living in GPU memory as a VA surface, for a
	// renderer that can draw it without a round trip through system memory.
	// Emitted instead of frameReady() when GPU rendering is in use.
	void hwFrameReady(AVFramePtr frame);
#endif

	void errorOccurred(QString message);

	// Says in plain words which path capture or playback actually settled on,
	// for the settings dialog to show. A request for GPU work can silently
	// come back as software, and forwarding a compressed camera stream skips
	// encoding altogether - neither should have to be guessed at.
	void accelerationInUse(QString description);

public:
	virtual QList<MediaDeviceInfo> Enumerate(enum MediaDeviceStreamDirection dir);

	// Name of the FFmpeg libavdevice demuxer for the host platform's native
	// video capture API. Counterpart of AudioDevice::PlatformDriverName().
	static QString PlatformDriverName();

	// Compressed formats the given camera can deliver by itself, as FFmpeg
	// codec names, ordered by preference: the more efficient the codec, the
	// earlier it comes. Empty if the camera offers none, or on platforms
	// where the formats cannot be queried - in which case captured frames
	// are decoded and re-encoded as usual.
	static QStringList CompressedFormats(const QString &device);

	// Every picture size the given camera can deliver, largest first, with the
	// frame rates and compressed formats available at each. Empty on a
	// platform where the camera cannot be asked - the caller then falls back
	// to offering a set of common sizes, which the camera is free to refuse
	// the same way it may refuse any other request.
	static QList<VideoCaptureFormat> CaptureFormats(const QString &device);

	// Whether the FFmpeg the client runs with was built with what one
	// acceleration flag needs in the given direction: a hardware encoder for at
	// least one of the call codecs, or the hardware context and a hwaccel for
	// at least one of their decoders. Says nothing about whether this machine's
	// GPU can use it - only whether there is anything to try at all, which is
	// what the settings dialog needs to know before offering it.
	static bool AccelerationBuiltIn(VideoAcceleration acceleration, bool encoding);

private:
	QString m_deviceName;
	QString m_codec;
	int m_width;
	int m_height;
	int m_frameRate;
	int m_bitrate;
	bool m_preferCompressed;
	// VideoAcceleration flags; which of them apply depends on the direction
	// this instance was configured for.
	int m_acceleration;
	bool m_useVaapiRender;

	QAtomicInt m_abort;
	// Set from another thread by disableHardwareRendering() while the decode
	// loop is running, so it has to be read atomically rather than as a plain
	// bool like the settings above, which are fixed before the loop starts.
	QAtomicInt m_hwRenderDisabled;
	// Likewise set from another thread, by setMaxFragmentSize(), while the
	// capture loop is running.
	QAtomicInt m_maxFragmentSize;

	// Likewise set from another thread, by setDisplaySize(), while the decode
	// loop is running. Both dimensions live in the one atomic - width in the
	// upper half, height in the lower - so that the decode loop cannot read a
	// new width against an old height and scale one frame to a wrong aspect
	// ratio. Zero means not known yet.
	QAtomicInt m_displaySize;

	QMutex m_queueMutex;
	QWaitCondition m_queueCond;
	QQueue<QByteArray> m_frameQueue;

	// Steady state: the newest frame wins, because for live video showing the
	// current picture matters more than showing every picture.
	static const int kMaxQueueDepth = 2;

	// Until the decode loop has taken its first frame it is not behind - it is
	// still opening the codec, which happens only after the peer's 0x75 has
	// been processed, by which time its packets are already arriving. Applying
	// the rule above during that window drops the stream's first keyframe and
	// then the frames that follow it, which costs every picture until the next
	// keyframe: with a camera's own stream forwarded untouched that can be ten
	// seconds away. So a bounded backlog is held instead - a couple of seconds
	// at a typical call frame rate - and the queue reverts to the depth above
	// as soon as the decoder is actually consuming.
	static const int kStartupQueueDepth = 30;

	// Whether the decode loop has taken a frame yet; see enqueueFrame(). Read
	// and written from both the decoding thread and whichever thread delivers
	// packets, hence atomic.
	QAtomicInt m_decoderStarted;

	// What setDisplaySize() was last given, unpacked; an empty size if it has
	// not been called. Read by the decode loop, once per frame.
	QSize displaySize() const;

	// Sends one encoded frame out as one or more protocol sized fragments.
	void emitFragments(const unsigned char *data, int size);

	// The two shapes Capture() can take: forwarding the camera's own
	// compressed stream as it arrives, or decoding what the camera sends and
	// re-encoding it. Both emit captureStarted() before their first packet.
	void runPassthroughLoop(struct AVFormatContext *ifmt_ctx, struct AVStream *in_st, const QString &codec);
	void runTranscodeLoop(struct AVFormatContext *ifmt_ctx, struct AVStream *in_st);
};

#endif // VIDEODEVICE_H
