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
#include "videodevice.h"
#include "../PICA_proto.h"
#include "../PICA_media.h"

#include <QFile>
#include <QDebug>
#include <QMutexLocker>
#include <cstring>

#ifdef Q_OS_LINUX
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Added to the kernel headers later than the formats around it, so define it
// here when building against an older set.
#ifndef V4L2_PIX_FMT_HEVC
#define V4L2_PIX_FMT_HEVC v4l2_fourcc('H', 'E', 'V', 'C')
#endif
#endif

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
#ifdef HAVE_VAAPI
#include <libavutil/hwcontext.h>
#endif
}

// Encoding defaults. Not exposed as settings yet - see the deferred list in
// the feature plan; a fixed, widely supported set of parameters for now.
static const int kFrameRate = 15;
static const int64_t kBitrate = 600000;
// Keyframe every ~2 seconds. There is no retransmission or keyframe request
// machinery yet, so this doubles as how quickly a receiver that missed data
// can resynchronize.
static const int kGopSize = kFrameRate * 2;

// Largest amount of encoded data one 0x77 message can carry over a c2c or
// directc2c connection: the protocol's per-message payload cap, minus the
// sequence number + timestamp header that every call media packet carries.
// Over a mediac2c connection the limit is the path MTU instead and is set
// with setMaxFragmentSize() once that connection is up.
static const int kMaxFragmentSize = PICA_PROTO_C2CMSG_MAXDATASIZE - PICA_PROTO_CALL_PACKET_HDRSIZE;

// Largest encoded slice the software encoder is asked to produce: one that
// still fits into a single datagram of a mediac2c connection, so that losing
// a datagram costs a slice of the picture rather than a whole frame.
static const int kSliceMaxSize = PICA_MEDIA_SAFE_PAYLOAD;

static QString ff_errstr(int err)
{
	char buf[256] = {0};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromLocal8Bit(buf);
}

#ifdef Q_OS_LINUX
// Compressed formats worth forwarding untouched, best first: HEVC gives the
// most picture per byte, H.264 is the widely supported middle ground, and
// MJPEG - every frame a standalone JPEG - costs far more bandwidth but still
// beats decoding and re-encoding. The names are FFmpeg codec names, as used
// by the v4l2 demuxer's input_format option and by the 0x75 protocol message.
static const struct
{
	unsigned int v4l2_pixelformat;
	const char *ffmpeg_codec;
} kCompressedFormatPreference[] =
{
	{ V4L2_PIX_FMT_HEVC,  "hevc"  },
	{ V4L2_PIX_FMT_H264,  "h264"  },
	{ V4L2_PIX_FMT_MJPEG, "mjpeg" },
	// Some cameras report plain JPEG rather than MJPEG for the same stream.
	{ V4L2_PIX_FMT_JPEG,  "mjpeg" }
};
#endif

#ifdef HAVE_VAAPI
// Picks the GPU pixel format out of what the decoder offers. Returning the
// first entry instead - which is what happens when VAAPI is not among them -
// leaves the decoder working in software, which is the wanted fallback.
static AVPixelFormat vaapi_get_format(AVCodecContext *ctx, const AVPixelFormat *formats)
{
	Q_UNUSED(ctx);

	for (const AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++)
	{
		if (*p == AV_PIX_FMT_VAAPI)
			return *p;
	}

	qWarning() << "The GPU cannot decode this stream, decoding in software";

	return formats[0];
}
#endif

QStringList VideoDevice::CompressedFormats(const QString &device)
{
	QStringList result;

#ifdef Q_OS_LINUX
	int fd = ::open(device.toLatin1().constData(), O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return result;

	// Collect what the camera offers first, then report it in preference
	// order rather than in the order the camera happens to list it.
	QList<unsigned int> offered;
	struct v4l2_fmtdesc fmt;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	for (fmt.index = 0; ::ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++)
	{
		if (fmt.flags & V4L2_FMT_FLAG_COMPRESSED)
			offered << fmt.pixelformat;
	}
	::close(fd);

	for (unsigned int i = 0; i < sizeof(kCompressedFormatPreference) / sizeof(kCompressedFormatPreference[0]); i++)
	{
		QString codec = QLatin1String(kCompressedFormatPreference[i].ffmpeg_codec);

		if (offered.contains(kCompressedFormatPreference[i].v4l2_pixelformat) && !result.contains(codec))
			result << codec;
	}
#else
	Q_UNUSED(device);
#endif

	return result;
}

VideoFrameAssembler::VideoFrameAssembler()
	: m_timestamp(0), m_inProgress(false), m_lastSeq(0),
	  m_lastWasFrameEnd(false), m_haveLastSeq(false)
{
}

void VideoFrameAssembler::reset()
{
	m_buffer.clear();
	m_inProgress = false;
	m_haveLastSeq = false;
	// The first fragment received is as likely to be the middle of a frame as
	// its beginning, so assembly only starts after the first frame boundary.
	m_lastWasFrameEnd = false;
}

QByteArray VideoFrameAssembler::addFragment(quint16 seq_num, quint32 timestamp, const QByteArray &data)
{
	const quint16 counter = seq_num & SeqNumMask;
	const bool isLast = (seq_num & LastFragmentFlag) != 0;
	bool contiguous = true;

	// Over a mediac2c connection fragments can be lost, so a fragment is only
	// accepted if it directly follows the previous one. When it does not,
	// what is in the buffer is incomplete and everything up to the end of the
	// current frame has to be skipped: a fragment may only begin a frame if
	// the one before it ended a frame. Without that rule, losing a frame's
	// first fragment would make its second fragment look like the beginning
	// of a frame and hand the decoder a truncated one.
	if (m_haveLastSeq)
		contiguous = (counter == ((m_lastSeq + 1) & SeqNumMask));

	m_lastSeq = counter;
	m_haveLastSeq = true;

	if (!contiguous)
	{
		// Resynchronize at the next frame boundary. This fragment can still
		// start a frame itself, if the gap swallowed whole frames and it
		// happens to be a first fragment - which is what a matching new
		// timestamp on an empty buffer means.
		m_buffer.clear();
		m_inProgress = false;
		m_lastWasFrameEnd = false;
	}

	if (!m_lastWasFrameEnd && !m_inProgress)
	{
		// Still in the middle of a frame whose beginning was lost.
		m_lastWasFrameEnd = isLast;
		return QByteArray();
	}

	// Fragments of one encoded frame all carry the same timestamp; a
	// different one while a frame is still being assembled means the rest of
	// that frame is never coming, so drop what was collected and start over.
	if (m_inProgress && timestamp != m_timestamp)
		m_buffer.clear();

	m_buffer.append(data);
	m_timestamp = timestamp;
	m_inProgress = true;
	m_lastWasFrameEnd = isLast;

	if (!isLast)
		return QByteArray();

	QByteArray frame = m_buffer;

	m_buffer.clear();
	m_inProgress = false;

	return frame;
}

VideoDevice::VideoDevice(QObject *parent)
	: QObject(parent), m_width(640), m_height(480), m_preferCompressed(false),
	  m_useVaapi(false), m_useVaapiRender(false), m_abort(0),
	  m_maxFragmentSize(kMaxFragmentSize)
{
}

void VideoDevice::setMaxFragmentSize(int size)
{
	if (size <= 0 || size > kMaxFragmentSize)
		size = kMaxFragmentSize;

	m_maxFragmentSize.storeRelaxed(size);
}

VideoDevice::~VideoDevice()
{
	Close();
}

void VideoDevice::configureCapture(QString deviceName, int width, int height,
                                   bool preferCompressed, bool useVaapi)
{
	m_deviceName = deviceName;
	m_width = width;
	m_height = height;
	m_preferCompressed = preferCompressed;
	m_useVaapi = useVaapi;
}

void VideoDevice::configurePlayback(QString codec, int width, int height,
                                    bool useVaapi, bool useVaapiRender)
{
	m_codec = codec;
	m_width = width;
	m_height = height;
	// Drawing a VA surface requires having decoded into one in the first
	// place, so asking for GPU rendering turns on GPU decoding regardless.
	m_useVaapi = useVaapi || useVaapiRender;
	m_useVaapiRender = useVaapiRender;
	// A renderer that failed during an earlier call says nothing about this
	// one - it may well be a different window.
	m_hwRenderDisabled.storeRelaxed(0);
}

void VideoDevice::disableHardwareRendering()
{
	m_hwRenderDisabled.storeRelaxed(1);
}

void VideoDevice::enqueueFrame(QByteArray encodedFrame)
{
	QMutexLocker locker(&m_queueMutex);

	while (m_frameQueue.size() >= kMaxQueueDepth)
		m_frameQueue.dequeue();

	m_frameQueue.enqueue(encodedFrame);
	m_queueCond.wakeAll();
}

void VideoDevice::Close()
{
	m_abort.storeRelaxed(1);

	{
		QMutexLocker locker(&m_queueMutex);
		m_frameQueue.clear();
	}
	m_queueCond.wakeAll();
}

// The string to hand avformat_open_input() for a camera.
//
// DirectShow namespaces its devices by type, so a camera has to be asked for
// as "video=<name>" - the same name behind an "audio=" prefix would be a
// microphone. v4l2 and avfoundation take the device path or index as it comes,
// so the name is stored unprefixed and decorated here, which also keeps what
// the settings dialog shows and stores readable.
static QByteArray cameraOpenUrl(const QString &device)
{
#ifdef Q_OS_WIN
	return (QStringLiteral("video=") + device).toUtf8();
#else
	return device.toUtf8();
#endif
}

void VideoDevice::Capture()
{
	m_abort.storeRelaxed(0);

	avdevice_register_all();
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
	avcodec_register_all();
#endif

	const QByteArray deviceUtf8 = cameraOpenUrl(m_deviceName);
	const QByteArray driverUtf8 = VideoDevice::PlatformDriverName().toUtf8();
	const AVInputFormat *ifmt = av_find_input_format(driverUtf8.constData());
	if (!ifmt)
	{
		QString msg = QString("Could not find '%1' video input driver").arg(VideoDevice::PlatformDriverName());
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	// Ask the camera for a compressed stream when told to and it offers one:
	// its packets can then go straight onto the network, with no decoding and
	// re-encoding in between. The formats are queried rather than guessed, so
	// this asks for one the camera has already said it can deliver.
	QString passthroughCodec;

	if (m_preferCompressed)
	{
		QStringList formats = CompressedFormats(m_deviceName);

		if (!formats.isEmpty())
			passthroughCodec = formats.first();
	}

	AVDictionary *opts = nullptr;
	av_dict_set(&opts, "video_size", QString("%1x%2").arg(m_width).arg(m_height).toUtf8().constData(), 0);
	av_dict_set(&opts, "framerate", QString::number(kFrameRate).toUtf8().constData(), 0);
	if (!passthroughCodec.isEmpty())
		av_dict_set(&opts, "input_format", passthroughCodec.toUtf8().constData(), 0);

	AVFormatContext *ifmt_ctx = nullptr;
	int ret = avformat_open_input(&ifmt_ctx, deviceUtf8.constData(), ifmt, &opts);
	av_dict_free(&opts);

	if (ret < 0 && !passthroughCodec.isEmpty())
	{
		// The camera listed the format but will not actually hand it over at
		// this size or frame rate. Fall back to whatever it does give.
		qWarning() << QString("Camera '%1' would not deliver %2 (%3), falling back to re-encoding")
		              .arg(m_deviceName, passthroughCodec, ff_errstr(ret));
		passthroughCodec.clear();

		opts = nullptr;
		av_dict_set(&opts, "video_size", QString("%1x%2").arg(m_width).arg(m_height).toUtf8().constData(), 0);
		av_dict_set(&opts, "framerate", QString::number(kFrameRate).toUtf8().constData(), 0);

		ret = avformat_open_input(&ifmt_ctx, deviceUtf8.constData(), ifmt, &opts);
		av_dict_free(&opts);
	}

	if (ret < 0)
	{
		// Last resort: let the camera pick. DirectShow in particular will
		// refuse a video_size or framerate it cannot produce exactly, rather
		// than settling for the nearest it can, so a camera with an unusual
		// set of modes would otherwise give no picture at all. Whatever comes
		// back is scaled to the negotiated size by the transcode loop anyway.
		qWarning() << QString("Camera '%1' would not open at %2x%3 @%4 (%5), letting it choose")
		              .arg(m_deviceName).arg(m_width).arg(m_height).arg(kFrameRate).arg(ff_errstr(ret));

		ret = avformat_open_input(&ifmt_ctx, deviceUtf8.constData(), ifmt, nullptr);
	}

	if (ret < 0)
	{
		// Not fatal to the call - it simply proceeds without outgoing video.
		QString msg = QString("Could not open camera '%1': %2").arg(m_deviceName, ff_errstr(ret));

#ifdef Q_OS_WIN
		// A camera that enumerated but will not open is usually not a broken
		// camera. Windows will happily list a device while refusing to
		// instantiate it, which is what "Let desktop apps access your camera"
		// being switched off looks like from here - and what another
		// application already holding the camera looks like too. Neither is
		// distinguishable from a genuine I/O error at this level, so say so
		// rather than leaving the user with an error code that suggests
		// hardware trouble.
		msg += tr(" - check that \"Let desktop apps access your camera\" is enabled in the "
		          "Windows camera privacy settings, and that no other application is using it");
#endif

		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, nullptr)) < 0 || ifmt_ctx->nb_streams < 1)
	{
		QString msg = QString("Could not determine camera stream parameters: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	AVStream *in_st = ifmt_ctx->streams[0];

	if (!passthroughCodec.isEmpty())
		runPassthroughLoop(ifmt_ctx, in_st, passthroughCodec);
	else
		runTranscodeLoop(ifmt_ctx, in_st);

	avformat_close_input(&ifmt_ctx);
}

void VideoDevice::emitFragments(const unsigned char *data, int size)
{
	// One encoded frame goes out as one or more 0x77 messages; the final one
	// is flagged so the receiver knows the frame is complete (see the
	// fragmentation notes in the protocol doc).
	int offset = 0;
	// Read once per frame rather than per fragment, so that a transport
	// change in the middle of a frame cannot make the receiving side see a
	// frame whose fragments were sized by two different rules.
	const int maxFragment = m_maxFragmentSize.loadRelaxed();

	while (offset < size)
	{
		int chunk = qMin(maxFragment, size - offset);
		bool isLast = (offset + chunk >= size);

		emit packetReady(QByteArray((const char *)data + offset, chunk), isLast);
		offset += chunk;
	}
}

void VideoDevice::runPassthroughLoop(AVFormatContext *ifmt_ctx, AVStream *in_st, const QString &codec)
{
	// The camera decides the picture size here, since nothing rescales it on
	// the way out - announce what it actually produced.
	int width = in_st->codecpar->width > 0 ? in_st->codecpar->width : m_width;
	int height = in_st->codecpar->height > 0 ? in_st->codecpar->height : m_height;

	emit captureStarted(codec, width, height);

	// Nothing encodes here at all - the camera's own packets go out as they
	// arrive - so any hardware encoding setting simply has nothing to act on.
	emit accelerationInUse(QStringLiteral("no encoding, camera stream forwarded unchanged"));

	AVPacket *pkt = av_packet_alloc();
	bool loggedReadError = false;

	while (!m_abort.loadRelaxed())
	{
		int ret = av_read_frame(ifmt_ctx, pkt);
		if (ret < 0)
		{
			if (ret == AVERROR(EAGAIN))
				continue;
			if (!loggedReadError)
			{
				qWarning() << QString("Camera read failed: %1").arg(ff_errstr(ret));
				loggedReadError = true;
			}
			break;
		}

		emitFragments(pkt->data, pkt->size);
		av_packet_unref(pkt);
	}

	av_packet_free(&pkt);
}

void VideoDevice::runTranscodeLoop(AVFormatContext *ifmt_ctx, AVStream *in_st)
{
	int ret;

	// The camera hands us either raw frames or a compressed stream that is
	// not being forwarded as-is; either way it goes through a decoder here to
	// get raw pictures for the encoder.
	const AVCodec *cam_dec = avcodec_find_decoder(in_st->codecpar->codec_id);
	AVCodecContext *dec_ctx = cam_dec ? avcodec_alloc_context3(cam_dec) : nullptr;
	if (!cam_dec || !dec_ctx ||
	    avcodec_parameters_to_context(dec_ctx, in_st->codecpar) < 0 ||
	    avcodec_open2(dec_ctx, cam_dec, nullptr) < 0)
	{
		QString msg = "Could not open camera format decoder";
		qWarning() << msg;
		emit errorOccurred(msg);
		if (dec_ctx) avcodec_free_context(&dec_ctx);
		return;
	}

	// The GPU encoder is tried first when asked for, and anything that goes
	// wrong on the way - no such encoder built into FFmpeg, no usable VAAPI
	// device, no surface pool, the encoder refusing to open - just leaves
	// hardware false and takes the software path below.
	bool hardware = false;
	const AVCodec *enc = nullptr;
	AVCodecContext *enc_ctx = nullptr;
	AVBufferRef *hw_frames_ref = nullptr;

#ifdef HAVE_VAAPI
	if (m_useVaapi && VaapiContext::isAvailable())
	{
		enc = avcodec_find_encoder_by_name("h264_vaapi");
		AVBufferRef *hw_device_ref = enc ? VaapiContext::deviceRef() : nullptr;

		if (hw_device_ref)
		{
			enc_ctx = avcodec_alloc_context3(enc);
			hw_frames_ref = enc_ctx ? av_hwframe_ctx_alloc(hw_device_ref) : nullptr;

			if (hw_frames_ref)
			{
				// The pool the encoder draws its input surfaces from. NV12 is
				// what VAAPI encoders take; the frames themselves live in GPU
				// memory, which is what AV_PIX_FMT_VAAPI denotes.
				AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames_ref->data;
				frames_ctx->format = AV_PIX_FMT_VAAPI;
				frames_ctx->sw_format = AV_PIX_FMT_NV12;
				frames_ctx->width = m_width;
				frames_ctx->height = m_height;
				frames_ctx->initial_pool_size = 20;

				if (av_hwframe_ctx_init(hw_frames_ref) >= 0)
				{
					enc_ctx->width = m_width;
					enc_ctx->height = m_height;
					enc_ctx->pix_fmt = AV_PIX_FMT_VAAPI;
					enc_ctx->time_base = AVRational{1, kFrameRate};
					enc_ctx->framerate = AVRational{kFrameRate, 1};
					enc_ctx->bit_rate = kBitrate;
					enc_ctx->gop_size = kGopSize;
					enc_ctx->max_b_frames = 0;
					enc_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);

					// No preset/tune here: those are x264's own options and
					// mean nothing to a VAAPI encoder.
					if ((ret = avcodec_open2(enc_ctx, enc, nullptr)) >= 0)
						hardware = true;
					else
						qWarning() << QString("VAAPI H.264 encoder would not open (%1), encoding in software")
						              .arg(ff_errstr(ret));
				}
				else
				{
					qWarning() << "Could not create a VAAPI surface pool, encoding in software";
				}
			}

			av_buffer_unref(&hw_device_ref);
		}
		else if (enc)
		{
			qWarning() << "No usable VAAPI device, encoding in software";
		}
		else
		{
			qWarning() << "FFmpeg has no h264_vaapi encoder, encoding in software";
		}

		if (!hardware)
		{
			if (enc_ctx) avcodec_free_context(&enc_ctx);
			if (hw_frames_ref) av_buffer_unref(&hw_frames_ref);
			enc = nullptr;
		}
	}
#endif

	if (!hardware)
	{
		enc = avcodec_find_encoder_by_name("libx264");
		if (!enc)
			enc = avcodec_find_encoder(AV_CODEC_ID_H264);
		enc_ctx = enc ? avcodec_alloc_context3(enc) : nullptr;
		if (!enc || !enc_ctx)
		{
			QString msg = "H.264 encoder not available";
			qWarning() << msg;
			emit errorOccurred(msg);
			avcodec_free_context(&dec_ctx);
			return;
		}

		enc_ctx->width = m_width;
		enc_ctx->height = m_height;
		enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
		enc_ctx->time_base = AVRational{1, kFrameRate};
		enc_ctx->framerate = AVRational{kFrameRate, 1};
		enc_ctx->bit_rate = kBitrate;
		enc_ctx->gop_size = kGopSize;
		// B-frames reorder output, which would cost latency in a live call.
		enc_ctx->max_b_frames = 0;

		// Note the absence of AV_CODEC_FLAG_GLOBAL_HEADER: we want the parameter
		// sets repeated in-band with every keyframe, since the receiving side has
		// no out-of-band way to learn them and may start decoding mid-stream.
		av_opt_set(enc_ctx->priv_data, "preset", "veryfast", 0);
		av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);

		// Keeps every slice within one datagram of a mediac2c connection, so
		// that a lost fragment costs a slice of the picture rather than the
		// whole frame. Harmless when the media goes over TCP instead - it
		// only splits frames into more NAL units than strictly necessary.
		av_opt_set(enc_ctx->priv_data, "x264opts",
		           QString("slice-max-size=%1").arg(kSliceMaxSize).toUtf8().constData(), 0);

		if ((ret = avcodec_open2(enc_ctx, enc, nullptr)) < 0)
		{
			QString msg = QString("Could not open H.264 encoder: %1").arg(ff_errstr(ret));
			qWarning() << msg;
			emit errorOccurred(msg);
			avcodec_free_context(&enc_ctx);
			avcodec_free_context(&dec_ctx);
			return;
		}
	}

	emit accelerationInUse(hardware ? QStringLiteral("GPU encoding (VAAPI)")
	                                : QStringLiteral("CPU encoding (libx264)"));

	// Everything the camera sends is scaled to the encoder's size and re-encoded,
	// so this is what the peer will receive regardless of what the camera
	// produced. The codec name is taken from the encoder rather than written
	// out literally, so that changing the encoder above cannot leave the peer
	// being told something else - avcodec_get_name() yields exactly the
	// FFmpeg codec names the 0x75 message is defined in terms of.
	emit captureStarted(QLatin1String(avcodec_get_name(enc_ctx->codec_id)),
	                    enc_ctx->width, enc_ctx->height);

	// What the camera's frames are scaled into. With a hardware encoder this
	// is only a staging buffer in system memory - NV12, which is what VAAPI
	// takes - that gets uploaded to a GPU surface below; the encoder itself
	// consumes AV_PIX_FMT_VAAPI frames and never sees this one.
	AVPixelFormat sw_pix_fmt = hardware ? AV_PIX_FMT_NV12 : enc_ctx->pix_fmt;

	AVFrame *enc_frame = av_frame_alloc();
	if (enc_frame)
	{
		enc_frame->format = sw_pix_fmt;
		enc_frame->width = enc_ctx->width;
		enc_frame->height = enc_ctx->height;
	}
	if (!enc_frame || av_frame_get_buffer(enc_frame, 0) < 0)
	{
		QString msg = "Could not allocate encoder frame";
		qWarning() << msg;
		emit errorOccurred(msg);
		if (enc_frame) av_frame_free(&enc_frame);
		if (hw_frames_ref) av_buffer_unref(&hw_frames_ref);
		avcodec_free_context(&enc_ctx);
		avcodec_free_context(&dec_ctx);
		return;
	}

	SwsContext *sws = nullptr;
	AVPacket *in_pkt = av_packet_alloc();
	AVFrame *cam_frame = av_frame_alloc();
	AVPacket *out_pkt = av_packet_alloc();
	int64_t pts = 0;
	bool loggedReadError = false;
	bool loggedScaleError = false;
	bool loggedEncodeError = false;

	while (!m_abort.loadRelaxed())
	{
		ret = av_read_frame(ifmt_ctx, in_pkt);
		if (ret < 0)
		{
			if (ret == AVERROR(EAGAIN))
				continue;
			if (!loggedReadError)
			{
				qWarning() << QString("Camera read failed: %1").arg(ff_errstr(ret));
				loggedReadError = true;
			}
			break;
		}

		ret = avcodec_send_packet(dec_ctx, in_pkt);
		av_packet_unref(in_pkt);
		if (ret < 0)
			continue;

		while (avcodec_receive_frame(dec_ctx, cam_frame) == 0)
		{
			// Built lazily: the camera's actual frame format and size are
			// only known once a frame has been decoded, and may differ from
			// what was requested.
			if (!sws)
			{
				sws = sws_getContext(cam_frame->width, cam_frame->height, (AVPixelFormat)cam_frame->format,
				                     enc_ctx->width, enc_ctx->height, sw_pix_fmt,
				                     SWS_BILINEAR, nullptr, nullptr, nullptr);
				if (!sws)
				{
					if (!loggedScaleError)
					{
						qWarning() << "Could not set up capture scaler";
						loggedScaleError = true;
					}
					av_frame_unref(cam_frame);
					continue;
				}
			}

			if (av_frame_make_writable(enc_frame) < 0)
			{
				av_frame_unref(cam_frame);
				continue;
			}

			sws_scale(sws, cam_frame->data, cam_frame->linesize, 0, cam_frame->height,
			          enc_frame->data, enc_frame->linesize);
			av_frame_unref(cam_frame);

			enc_frame->pts = pts++;

			// What actually goes to the encoder: the staging frame itself in
			// software, or a GPU surface holding a copy of it in hardware.
			AVFrame *frame_to_encode = enc_frame;
#ifdef HAVE_VAAPI
			AVFrame *hw_frame = nullptr;

			if (hardware)
			{
				hw_frame = av_frame_alloc();

				if (!hw_frame ||
				    av_hwframe_get_buffer(enc_ctx->hw_frames_ctx, hw_frame, 0) < 0 ||
				    av_hwframe_transfer_data(hw_frame, enc_frame, 0) < 0)
				{
					if (!loggedEncodeError)
					{
						qWarning() << "Could not upload the frame to the GPU";
						loggedEncodeError = true;
					}
					if (hw_frame) av_frame_free(&hw_frame);
					continue;
				}

				hw_frame->pts = enc_frame->pts;
				frame_to_encode = hw_frame;
			}
#endif

			int send_ret = avcodec_send_frame(enc_ctx, frame_to_encode);
#ifdef HAVE_VAAPI
			if (hw_frame)
				av_frame_free(&hw_frame);
#endif

			if (send_ret != 0)
			{
				if (!loggedEncodeError)
				{
					qWarning() << "H.264 encode failed";
					loggedEncodeError = true;
				}
				continue;
			}

			while (avcodec_receive_packet(enc_ctx, out_pkt) == 0)
			{
				emitFragments(out_pkt->data, out_pkt->size);
				av_packet_unref(out_pkt);
			}
		}
	}

	av_packet_free(&in_pkt);
	av_packet_free(&out_pkt);
	av_frame_free(&cam_frame);
	av_frame_free(&enc_frame);
	if (sws) sws_freeContext(sws);
	if (hw_frames_ref) av_buffer_unref(&hw_frames_ref);
	avcodec_free_context(&enc_ctx);
	avcodec_free_context(&dec_ctx);
}

void VideoDevice::Play()
{
	m_abort.storeRelaxed(0);

#if LIBAVFORMAT_VERSION_MAJOR < 58
	avcodec_register_all();
#endif

	// The peer announces its codec by FFmpeg name in the 0x75 message, which
	// is exactly what avcodec_find_decoder_by_name() takes - so whatever the
	// other side ends up sending (its own camera's hevc/h264/mjpeg stream
	// forwarded untouched, or h264 it encoded itself) is handled here without
	// a codec list to keep in step.
	const AVCodec *dec = avcodec_find_decoder_by_name(m_codec.toLatin1().constData());
	if (dec && dec->type != AVMEDIA_TYPE_VIDEO)
		dec = nullptr;
	AVCodecContext *dec_ctx = dec ? avcodec_alloc_context3(dec) : nullptr;

#ifdef HAVE_VAAPI
	// Has to be attached before opening the decoder. Unlike encoding, there is
	// no separate hardware decoder to look up: the ordinary decoder is told to
	// use a VAAPI device, and get_format then picks the GPU pixel format when
	// the codec and hardware between them can manage it.
	if (m_useVaapi && dec_ctx && VaapiContext::isAvailable())
	{
		AVBufferRef *hw_device_ref = VaapiContext::deviceRef();

		if (hw_device_ref)
		{
			// The context takes ownership of this reference.
			dec_ctx->hw_device_ctx = hw_device_ref;
			dec_ctx->get_format = vaapi_get_format;
		}
		else
		{
			qWarning() << "No usable VAAPI device, decoding in software";
		}
	}
#endif

	if (!dec || !dec_ctx || avcodec_open2(dec_ctx, dec, nullptr) < 0)
	{
		QString msg = QString("Unsupported call video codec: %1").arg(m_codec);
		qWarning() << msg;
		emit errorOccurred(msg);
		if (dec_ctx) avcodec_free_context(&dec_ctx);
		return;
	}

	SwsContext *sws = nullptr;
	int sws_width = 0, sws_height = 0;
	AVPixelFormat sws_fmt = AV_PIX_FMT_NONE;
	AVFrame *dec_frame = av_frame_alloc();
	AVFrame *rgb_frame = av_frame_alloc();
	bool loggedDecodeError = false;
	bool loggedScaleError = false;
	bool loggedTransferError = false;
	// Whether the GPU is really doing the decoding is only knowable from a
	// decoded frame, so the answer is reported once the first one arrives
	// rather than guessed from what was asked for.
	bool reportedAcceleration = false;

	while (true)
	{
		QByteArray frameData;
		{
			QMutexLocker locker(&m_queueMutex);
			while (m_frameQueue.isEmpty() && !m_abort.loadRelaxed())
				m_queueCond.wait(&m_queueMutex, 100);

			if (m_abort.loadRelaxed())
				break;

			if (m_frameQueue.isEmpty())
				continue;

			frameData = m_frameQueue.dequeue();
		}

		AVPacket *in_pkt = av_packet_alloc();
		if (av_new_packet(in_pkt, frameData.size()) < 0)
		{
			av_packet_free(&in_pkt);
			continue;
		}
		memcpy(in_pkt->data, frameData.constData(), frameData.size());

		int ret = avcodec_send_packet(dec_ctx, in_pkt);
		av_packet_free(&in_pkt);
		if (ret < 0)
		{
			if (!loggedDecodeError)
			{
				qWarning() << QString("H.264 decode failed: %1").arg(ff_errstr(ret));
				loggedDecodeError = true;
			}
			continue;
		}

		while (avcodec_receive_frame(dec_ctx, dec_frame) == 0)
		{
			bool decodedOnGpu = false;
#ifdef HAVE_VAAPI
			decodedOnGpu = (dec_frame->format == AV_PIX_FMT_VAAPI);
#endif

			if (!reportedAcceleration)
			{
				emit accelerationInUse(decodedOnGpu ? QStringLiteral("GPU decoding (VAAPI)")
				                                    : QStringLiteral("CPU decoding"));
				reportedAcceleration = true;
			}

			// The frame the rest of this iteration works from. A GPU frame
			// holds no pixels, so unless it is going straight to a renderer
			// that can draw it, it has to be brought down into system memory
			// first.
			AVFrame *display_frame = dec_frame;
#ifdef HAVE_VAAPI
			AVFrame *sw_frame = nullptr;

			if (decodedOnGpu && m_useVaapiRender && !m_hwRenderDisabled.loadRelaxed())
			{
				// Zero copy: hand the surface itself over, holding a
				// reference so it stays alive until the renderer is done.
				AVFrame *hw_ref = av_frame_alloc();

				if (hw_ref && av_frame_ref(hw_ref, dec_frame) == 0)
				{
					emit hwFrameReady(wrapAVFrame(hw_ref));
				}
				else if (hw_ref)
				{
					av_frame_free(&hw_ref);
				}

				av_frame_unref(dec_frame);
				continue;
			}

			if (decodedOnGpu)
			{
				sw_frame = av_frame_alloc();

				if (!sw_frame || av_hwframe_transfer_data(sw_frame, dec_frame, 0) < 0)
				{
					if (!loggedTransferError)
					{
						qWarning() << "Could not read the decoded frame back from the GPU";
						loggedTransferError = true;
					}
					if (sw_frame) av_frame_free(&sw_frame);
					av_frame_unref(dec_frame);
					continue;
				}

				display_frame = sw_frame;
			}
#endif

			// Rebuilt whenever the incoming picture format or size changes -
			// including the first frame, when they first become known.
			if (!sws || display_frame->width != sws_width || display_frame->height != sws_height ||
			    (AVPixelFormat)display_frame->format != sws_fmt)
			{
				if (sws) sws_freeContext(sws);
				sws_width = display_frame->width;
				sws_height = display_frame->height;
				sws_fmt = (AVPixelFormat)display_frame->format;

				sws = sws_getContext(sws_width, sws_height, sws_fmt,
				                     sws_width, sws_height, AV_PIX_FMT_RGB24,
				                     SWS_BILINEAR, nullptr, nullptr, nullptr);

				av_frame_unref(rgb_frame);
				rgb_frame->format = AV_PIX_FMT_RGB24;
				rgb_frame->width = sws_width;
				rgb_frame->height = sws_height;

				if (!sws || av_frame_get_buffer(rgb_frame, 0) < 0)
				{
					if (!loggedScaleError)
					{
						qWarning() << "Could not set up playback scaler";
						loggedScaleError = true;
					}
					if (sws) { sws_freeContext(sws); sws = nullptr; }
#ifdef HAVE_VAAPI
					if (sw_frame) av_frame_free(&sw_frame);
#endif
					av_frame_unref(dec_frame);
					continue;
				}
			}

			if (av_frame_make_writable(rgb_frame) < 0)
			{
#ifdef HAVE_VAAPI
				if (sw_frame) av_frame_free(&sw_frame);
#endif
				av_frame_unref(dec_frame);
				continue;
			}

			sws_scale(sws, display_frame->data, display_frame->linesize, 0, display_frame->height,
			          rgb_frame->data, rgb_frame->linesize);

			// Deep copy: the QImage outlives this iteration (it travels to
			// the GUI thread through a queued signal), while rgb_frame's
			// buffer gets reused by the next scale.
			QImage img(rgb_frame->data[0], sws_width, sws_height,
			           rgb_frame->linesize[0], QImage::Format_RGB888);
			emit frameReady(img.copy());

#ifdef HAVE_VAAPI
			if (sw_frame) av_frame_free(&sw_frame);
#endif
			av_frame_unref(dec_frame);
		}
	}

	av_frame_free(&dec_frame);
	av_frame_free(&rgb_frame);
	if (sws) sws_freeContext(sws);
	avcodec_free_context(&dec_ctx);
}

#ifdef Q_OS_WIN
// Defined in videodshow.cpp - FFmpeg's dshow demuxer can capture but cannot
// enumerate, so the list comes from DirectShow itself.
QList<MediaDeviceInfo> pica_enumerate_dshow_video();
#endif

QList<MediaDeviceInfo> VideoDevice::Enumerate(enum MediaDeviceStreamDirection dir)
{
	QList<MediaDeviceInfo> result;
	int index = 0;

#ifdef Q_OS_WIN
	Q_UNUSED(index)

	// Cameras are capture only; nothing renders video to a device here.
	if (dir != PLAYBACK)
		result = pica_enumerate_dshow_video();

	return result;
#endif

	if (dir == PLAYBACK)
		return result;
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
			d.compressedFormats = CompressedFormats(d.device);

			result << d;
		}
	}
#endif
	return result;
}

QString VideoDevice::PlatformDriverName()
{
#if defined(Q_OS_LINUX)
	return QStringLiteral("v4l2");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	return QStringLiteral("avfoundation");
#elif defined(Q_OS_WIN)
	return QStringLiteral("dshow");
#else
	return QStringLiteral("v4l2");
#endif
}
