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
#include "callsettings.h"
#include "../PICA_proto.h"
#include "../PICA_media.h"

#include <QFile>
#include <QDebug>
#include <QMutexLocker>
#include <QMap>
#include <QPair>
#include <cstring>
#include <algorithm>
#include <functional>

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

#ifdef Q_OS_WIN
// Both defined in videodshow.cpp. FFmpeg's dshow demuxer can capture but can
// neither enumerate the cameras nor say what formats one offers, so both
// answers come from DirectShow itself.
QList<MediaDeviceInfo> pica_enumerate_dshow_video();
QStringList pica_dshow_compressed_formats(const QString &device);
#endif

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

// Which FFmpeg encoder produces each of the codecs the "Video Codec" setting
// offers, on the GPU and in software. Two names per codec because FFmpeg names
// its external library encoders after the library rather than after the codec,
// and the id as well so that a build without the library named here can still
// fall back to whatever other encoder it has for the same codec.
static const struct
{
	const char *codec;
	AVCodecID id;
	const char *vaapiEncoder;
	const char *swEncoder;
} kVideoEncoders[] =
{
	{ "h264", AV_CODEC_ID_H264, "h264_vaapi", "libx264"    },
	{ "hevc", AV_CODEC_ID_HEVC, "hevc_vaapi", "libx265"    },
	{ "vp9",  AV_CODEC_ID_VP9,  "vp9_vaapi",  "libvpx-vp9" }
};

static int videoEncoderIndex(const QString &codec)
{
	for (unsigned int i = 0; i < sizeof(kVideoEncoders) / sizeof(kVideoEncoders[0]); i++)
	{
		if (codec == QLatin1String(kVideoEncoders[i].codec))
			return (int)i;
	}

	// An unrecognised setting - written by a build offering something this one
	// does not, or by hand - falls back to the codec every peer can decode
	// rather than leaving the call without video.
	return 0;
}

// Sets the private options that make an encoder suitable for a live call:
// low latency, no frame reordering, and parameter sets the receiver can pick
// up mid-stream. Each of the three encoders spells all of that differently,
// which is why this is keyed on the encoder rather than on the codec.
static void applyLiveEncoderOptions(AVCodecContext *enc_ctx, const QString &encoderName, int sliceMaxSize)
{
	if (encoderName == QLatin1String("libx264"))
	{
		av_opt_set(enc_ctx->priv_data, "preset", "veryfast", 0);
		av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);

		// Keeps every slice within one datagram of a mediac2c connection, so
		// that a lost fragment costs a slice of the picture rather than the
		// whole frame. Harmless when the media goes over TCP instead - it
		// only splits frames into more NAL units than strictly necessary.
		av_opt_set(enc_ctx->priv_data, "x264opts",
		           QString("slice-max-size=%1").arg(sliceMaxSize).toUtf8().constData(), 0);
	}
	else if (encoderName == QLatin1String("libx265"))
	{
		av_opt_set(enc_ctx->priv_data, "preset", "ultrafast", 0);
		av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);

		// repeat-headers is the x265 equivalent of leaving
		// AV_CODEC_FLAG_GLOBAL_HEADER off below: without it the VPS/SPS/PPS go
		// out once, at the start, and a receiver that joins the stream later -
		// which every receiver does, the call being already running by the
		// time its decoder opens - never sees them.
		//
		// x265 has no slice-max-size; the nearest thing is a fixed number of
		// slices per frame, which does not bound their size. Fragmentation
		// handles oversized frames either way, a loss just costs more of the
		// picture than it does with x264.
		av_opt_set(enc_ctx->priv_data, "x265-params", "repeat-headers=1:bframes=0", 0);
	}
	else if (encoderName == QLatin1String("libvpx-vp9"))
	{
		// "realtime" with a high cpu-used is libvpx's zerolatency: it caps how
		// long the encoder may spend per frame rather than letting it take as
		// long as the quality target needs.
		av_opt_set(enc_ctx->priv_data, "deadline", "realtime", 0);
		av_opt_set_int(enc_ctx->priv_data, "cpu-used", 8, 0);
		// No lookahead - it would hold frames back, which is latency in a
		// live call - and keep the bitstream decodable across a loss.
		av_opt_set_int(enc_ctx->priv_data, "lag-in-frames", 0, 0);
		av_opt_set_int(enc_ctx->priv_data, "error-resilient", 1, 0);

		// VP9 carries no parameter sets to repeat: everything a decoder needs
		// is in the keyframe itself, so joining mid-stream needs nothing
		// special here.
	}
}

#ifdef Q_OS_WIN
// Defined in videodshow.cpp, alongside the enumeration functions - see the
// comment on pica_enumerate_dshow_video() for why the answers come from
// DirectShow rather than from FFmpeg's demuxer.
QList<VideoCaptureFormat> pica_dshow_capture_formats(const QString &device);
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
// Frame rates worth offering out of a camera that reports a continuous range
// rather than a list of what it supports. Such a camera will take anything in
// between, so this is only a question of what to put in a dropdown.
static const int kStandardFrameRates[] = { 60, 50, 30, 25, 24, 20, 15, 10, 5 };

// Sizes offered for the same reason, out of a camera that reports a range of
// sizes rather than a list. Ones outside the range, or that the reported step
// does not land on, are dropped by the caller.
static const struct
{
	int width;
	int height;
} kStandardSizes[] =
{
	{ 1920, 1080 }, { 1280, 720 }, { 1024, 768 }, { 800, 600 },
	{ 848, 480 }, { 640, 480 }, { 640, 360 }, { 424, 240 },
	{ 352, 288 }, { 320, 240 }, { 176, 144 }, { 160, 120 }
};

// Largest picture first, and highest frame rate first within each.
static bool captureFormatLessThan(const VideoCaptureFormat &a, const VideoCaptureFormat &b)
{
	const qint64 areaA = (qint64)a.width * a.height;
	const qint64 areaB = (qint64)b.width * b.height;

	if (areaA != areaB)
		return areaA > areaB;

	return a.width > b.width;
}
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
#elif defined(Q_OS_WIN)
	result = pica_dshow_compressed_formats(device);
#else
	Q_UNUSED(device);
#endif

	return result;
}

#ifdef Q_OS_LINUX
// Every picture size the camera offers in one pixel format.
//
// A camera answers this in one of two ways: a list of the sizes it has, or a
// range it will scale anything within. There is no list to show for the
// second, so the standard sizes that fall inside the range stand in for one.
static QList<QPair<int, int> > v4l2FrameSizes(int fd, unsigned int pixelformat)
{
	QList<QPair<int, int> > sizes;
	struct v4l2_frmsizeenum fs;

	memset(&fs, 0, sizeof fs);
	fs.pixel_format = pixelformat;

	for (fs.index = 0; ::ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0; fs.index++)
	{
		if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE)
		{
			sizes << qMakePair((int)fs.discrete.width, (int)fs.discrete.height);
			continue;
		}

		for (unsigned int i = 0; i < sizeof(kStandardSizes) / sizeof(kStandardSizes[0]); i++)
		{
			const unsigned int w = kStandardSizes[i].width;
			const unsigned int h = kStandardSizes[i].height;

			if (w < fs.stepwise.min_width || w > fs.stepwise.max_width ||
			    h < fs.stepwise.min_height || h > fs.stepwise.max_height)
				continue;

			// A continuous camera reports a step of one, so this only ever
			// excludes anything on a genuinely stepwise one.
			if (fs.stepwise.step_width > 1 && (w - fs.stepwise.min_width) % fs.stepwise.step_width)
				continue;

			if (fs.stepwise.step_height > 1 && (h - fs.stepwise.min_height) % fs.stepwise.step_height)
				continue;

			sizes << qMakePair((int)w, (int)h);
		}

		// A non-discrete answer is a single entry describing the whole range;
		// there is no index 1 to ask for.
		break;
	}

	return sizes;
}

// v4l2 reports frame intervals - seconds per frame - and we want the rate, so
// every one of these is a reciprocal.
static double v4l2IntervalToFps(const struct v4l2_fract &f)
{
	if (f.numerator == 0 || f.denominator == 0)
		return 0.0;

	return (double)f.denominator / (double)f.numerator;
}

// The frame rates the camera offers for one pixel format at one size, highest
// first. Same two shapes of answer as the sizes above, handled the same way.
static QList<int> v4l2FrameRates(int fd, unsigned int pixelformat, int width, int height)
{
	QList<int> rates;
	struct v4l2_frmivalenum fi;

	memset(&fi, 0, sizeof fi);
	fi.pixel_format = pixelformat;
	fi.width = width;
	fi.height = height;

	for (fi.index = 0; ::ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; fi.index++)
	{
		if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE)
		{
			int fps = qRound(v4l2IntervalToFps(fi.discrete));

			if (fps > 0 && !rates.contains(fps))
				rates << fps;

			continue;
		}

		// The longest interval is the lowest rate, hence the crossed over
		// names here.
		const double minFps = v4l2IntervalToFps(fi.stepwise.max);
		const double maxFps = v4l2IntervalToFps(fi.stepwise.min);

		for (unsigned int i = 0; i < sizeof(kStandardFrameRates) / sizeof(kStandardFrameRates[0]); i++)
		{
			const int fps = kStandardFrameRates[i];

			if (fps >= minFps && fps <= maxFps && !rates.contains(fps))
				rates << fps;
		}

		break;
	}

	std::sort(rates.begin(), rates.end(), std::greater<int>());

	return rates;
}
#endif

QList<VideoCaptureFormat> VideoDevice::CaptureFormats(const QString &device)
{
	QList<VideoCaptureFormat> result;

#ifdef Q_OS_LINUX
	int fd = ::open(device.toLatin1().constData(), O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return result;

	// Keyed on the size, because a camera reports its sizes once per pixel
	// format and the same size usually comes back several times over. What
	// differs between those is the format and the frame rates available in
	// it, so both are merged into the one entry for the size.
	QMap<QPair<int, int>, VideoCaptureFormat> bySize;

	struct v4l2_fmtdesc fmt;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	for (fmt.index = 0; ::ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++)
	{
		// What this pixel format is called in a 0x75 message, empty for an
		// uncompressed one and for a compressed one we would not forward.
		QString codec;

		for (unsigned int i = 0; i < sizeof(kCompressedFormatPreference) / sizeof(kCompressedFormatPreference[0]); i++)
		{
			if (kCompressedFormatPreference[i].v4l2_pixelformat == fmt.pixelformat)
			{
				codec = QLatin1String(kCompressedFormatPreference[i].ffmpeg_codec);
				break;
			}
		}

		QList<QPair<int, int> > sizes = v4l2FrameSizes(fd, fmt.pixelformat);

		for (int s = 0; s < sizes.size(); s++)
		{
			VideoCaptureFormat &entry = bySize[sizes.at(s)];

			entry.width = sizes.at(s).first;
			entry.height = sizes.at(s).second;

			if (!codec.isEmpty() && !entry.compressedFormats.contains(codec))
				entry.compressedFormats << codec;

			QList<int> rates = v4l2FrameRates(fd, fmt.pixelformat, entry.width, entry.height);

			for (int r = 0; r < rates.size(); r++)
				if (!entry.frameRates.contains(rates.at(r)))
					entry.frameRates << rates.at(r);
		}
	}

	::close(fd);

	for (QMap<QPair<int, int>, VideoCaptureFormat>::iterator it = bySize.begin(); it != bySize.end(); ++it)
	{
		VideoCaptureFormat entry = it.value();

		// The formats were collected in whatever order the camera listed them
		// in; report them in preference order, the same one
		// CompressedFormats() and VideoDevice::Capture() work in.
		QStringList ordered;

		for (unsigned int i = 0; i < sizeof(kCompressedFormatPreference) / sizeof(kCompressedFormatPreference[0]); i++)
		{
			QString codec = QLatin1String(kCompressedFormatPreference[i].ffmpeg_codec);

			if (entry.compressedFormats.contains(codec) && !ordered.contains(codec))
				ordered << codec;
		}

		entry.compressedFormats = ordered;

		std::sort(entry.frameRates.begin(), entry.frameRates.end(), std::greater<int>());

		result << entry;
	}

	std::sort(result.begin(), result.end(), captureFormatLessThan);
#elif defined(Q_OS_WIN)
	result = pica_dshow_capture_formats(device);
#else
	Q_UNUSED(device);
#endif

	return result;
}

VideoFrameAssembler::VideoFrameAssembler()
	: m_timestamp(0), m_lastSeq(0)
{
	// Through reset() rather than an initialiser list of its own: a fresh
	// assembler and one that has been reset are the same thing, and stating
	// that start-of-stream state twice is how the two came to disagree - the
	// constructor's copy is what the first call after launch actually uses.
	reset();
}

void VideoFrameAssembler::reset()
{
	m_buffer.clear();
	m_inProgress = false;
	m_haveLastSeq = false;

	// The next fragment is taken to begin a frame, rather than to be the
	// middle of one whose start was lost.
	//
	// Every caller resets a stream that is about to start, never one being
	// joined in flight - a call resets before the first packet can arrive, and
	// the switch to a mediac2c connection deliberately does not reset, since
	// media keeps arriving over either transport for the whole call. So the
	// first fragment after this really is the first one the sender sent.
	//
	// Assuming the opposite costs the stream's first frame, and that frame is
	// the one that matters: with a camera's own compressed stream being
	// forwarded untouched, it carries the parameter sets and the only keyframe
	// the camera will produce for the next ten seconds, so losing it means ten
	// seconds of black. If this assumption is ever wrong the cost is one
	// truncated frame that the decoder rejects, and the next frame boundary
	// resynchronizes as usual.
	m_lastWasFrameEnd = true;
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
	: QObject(parent), m_width(kDefaultCaptureWidth), m_height(kDefaultCaptureHeight),
	  m_frameRate(kDefaultCaptureFrameRate), m_bitrate(kDefaultVideoBitrateKbps * 1000),
	  m_preferCompressed(false),
	  m_useVaapi(false), m_useVaapiRender(false), m_abort(0),
	  m_maxFragmentSize(kMaxFragmentSize), m_decoderStarted(0)
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

void VideoDevice::configureCapture(QString deviceName, int width, int height, int frameRate,
                                   bool preferCompressed, QString codec, int bitrate,
                                   bool useVaapi)
{
	m_deviceName = deviceName;
	m_width = width;
	m_height = height;
	m_frameRate = frameRate > 0 ? frameRate : kDefaultCaptureFrameRate;
	m_codec = codec;
	m_bitrate = bitrate > 0 ? bitrate : kDefaultVideoBitrateKbps * 1000;
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
	// A fresh decode session, so the startup queue depth applies again until
	// this one's loop is consuming - see enqueueFrame().
	m_decoderStarted.storeRelaxed(0);
}

void VideoDevice::disableHardwareRendering()
{
	m_hwRenderDisabled.storeRelaxed(1);
}

void VideoDevice::enqueueFrame(QByteArray encodedFrame)
{
	QMutexLocker locker(&m_queueMutex);

	// Deeper while the decoder is still starting up - see kStartupQueueDepth.
	const int depth = m_decoderStarted.loadRelaxed() ? kMaxQueueDepth : kStartupQueueDepth;

	while (m_frameQueue.size() >= depth)
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

// One attempt at opening the camera.
//
// passthroughCodec empty asks for whatever the camera gives; a width of zero
// drops the size and frame rate constraints as well. *ifmt_ctx is left null on
// failure - avformat_open_input() frees and clears it - so the caller can just
// try again.
static int openCamera(const AVInputFormat *ifmt, const QByteArray &url,
                      int width, int height, int framerate,
                      const QString &passthroughCodec, AVFormatContext **ifmt_ctx)
{
	AVDictionary *opts = nullptr;

	if (width > 0 && height > 0)
		av_dict_set(&opts, "video_size", QString("%1x%2").arg(width).arg(height).toUtf8().constData(), 0);

	if (framerate > 0)
		av_dict_set(&opts, "framerate", QString::number(framerate).toUtf8().constData(), 0);

	if (!passthroughCodec.isEmpty())
	{
#ifdef Q_OS_WIN
		// dshow has no input_format option. It picks the compressed stream
		// from AVFormatContext::video_codec_id instead, which is what
		// "-vcodec mjpeg" ahead of "-i" sets on the command line - and that
		// has to be set on a context allocated here, since
		// avformat_open_input() would otherwise make one only after the point
		// where the demuxer reads it.
		const AVCodec *dec = avcodec_find_decoder_by_name(passthroughCodec.toUtf8().constData());

		if (!dec)
		{
			av_dict_free(&opts);
			return AVERROR(EINVAL);
		}

		*ifmt_ctx = avformat_alloc_context();

		if (!*ifmt_ctx)
		{
			av_dict_free(&opts);
			return AVERROR(ENOMEM);
		}

		(*ifmt_ctx)->video_codec_id = dec->id;
#else
		av_dict_set(&opts, "input_format", passthroughCodec.toUtf8().constData(), 0);
#endif
	}

	int ret = avformat_open_input(ifmt_ctx, url.constData(), ifmt, &opts);
	av_dict_free(&opts);

	return ret;
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
	// these are ones the camera has already said it can deliver, best first.
	// The empty string on the end is the fallback of taking whatever comes and
	// re-encoding it.
	QStringList attempts;

	if (m_preferCompressed)
	{
		attempts = CompressedFormats(m_deviceName);

		// The configured codec first when the camera can produce it itself:
		// forwarding that one means the peer receives exactly what was asked
		// for, rather than whichever compressed format the camera happened to
		// rank highest. The rest keep their preference order behind it, since
		// forwarding any of them still beats decoding and re-encoding.
		if (attempts.removeAll(m_codec) > 0)
			attempts.prepend(m_codec);
	}

	attempts << QString();

	// Listing a format and being able to produce it at a given size and frame
	// rate are three separate claims, and cameras routinely honour the first
	// without the others - an integrated webcam offering MJPEG but not at
	// 640x480 15fps is what prompted this. So each candidate is tried with the
	// constraints loosened a step at a time, giving up the frame rate before
	// the size and both before giving up the format, since the format is worth
	// the most: it decides whether anything has to be re-encoded at all.
	//
	// Whatever comes back is what gets announced to the peer - see
	// captureStarted() and AudioVideoCallController::video_capture_started() -
	// so relaxing these changes what is sent, not what is claimed.
	static const struct
	{
		bool size;
		bool rate;
	} kConstraints[] =
	{
		{ true,  true  },
		{ true,  false },
		{ false, false },
	};

	QString passthroughCodec;
	AVFormatContext *ifmt_ctx = nullptr;
	int ret = AVERROR(EINVAL);
	bool opened = false;

	for (int a = 0; a < attempts.size() && !opened; a++)
	{
		for (unsigned int c = 0; c < sizeof(kConstraints) / sizeof(kConstraints[0]); c++)
		{
			ret = openCamera(ifmt, deviceUtf8,
			                 kConstraints[c].size ? m_width : 0,
			                 kConstraints[c].size ? m_height : 0,
			                 kConstraints[c].rate ? m_frameRate : 0,
			                 attempts.at(a), &ifmt_ctx);

			if (ret >= 0)
			{
				passthroughCodec = attempts.at(a);
				opened = true;
				break;
			}

			QString asked;

			if (!kConstraints[c].size)
				asked = QStringLiteral("any size or rate");
			else if (kConstraints[c].rate)
				asked = QString("%1x%2 @%3").arg(m_width).arg(m_height).arg(m_frameRate);
			else
				asked = QString("%1x%2 at any rate").arg(m_width).arg(m_height);

			qWarning() << QString("Camera '%1': no %2 at %3 (%4)")
			              .arg(m_deviceName,
			                   attempts.at(a).isEmpty() ? QStringLiteral("stream") : attempts.at(a),
			                   asked,
			                   ff_errstr(ret));
		}
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

	// Check what actually came back rather than trusting the request. A
	// camera can list a format and then open in a different one, and asking
	// for it is spelled differently per demuxer - so the one thing worth
	// relying on is the stream in front of us. Handing raw frames to the
	// passthrough loop would put unencoded video on the wire labelled as
	// something it is not.
	if (!passthroughCodec.isEmpty())
	{
		const AVCodec *wanted = avcodec_find_decoder_by_name(passthroughCodec.toUtf8().constData());

		if (!wanted || in_st->codecpar->codec_id != wanted->id)
		{
			qWarning() << QString("Camera '%1' opened as %2 rather than the %3 that was asked for, re-encoding")
			              .arg(m_deviceName,
			                   QLatin1String(avcodec_get_name(in_st->codecpar->codec_id)),
			                   passthroughCodec);
			passthroughCodec.clear();
		}
	}

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

// The frame rate the camera actually settled on, which is not necessarily the
// one that was asked for - and when the open had to fall back to letting the
// camera choose, was not asked for at all. Encoding at a rate the frames do
// not arrive at gives a stream whose timestamps disagree with reality.
static AVRational cameraFrameRate(AVStream *in_st, int fallbackRate)
{
	AVRational fr = in_st->avg_frame_rate;

	if (fr.num <= 0 || fr.den <= 0)
		fr = in_st->r_frame_rate;

	if (fr.num <= 0 || fr.den <= 0)
		fr = AVRational{ fallbackRate, 1 };

	return fr;
}

void VideoDevice::runTranscodeLoop(AVFormatContext *ifmt_ctx, AVStream *in_st)
{
	const AVRational frameRate = cameraFrameRate(in_st, m_frameRate);

	// Which encoder produces the configured codec. Looked up once: the GPU
	// attempt and the software fallback below are two encoders for the same
	// codec, so the peer is told the same thing either way.
	const int encIdx = videoEncoderIndex(m_codec);

	// Two seconds between keyframes. A receiver that joins late or loses a
	// packet waits this long for a picture, so it is a latency figure as much
	// as a bandwidth one - hence deriving it from the real rate rather than
	// from a fixed frame count.
	const int gopSize = qMax(1, (int)av_q2d(frameRate) * 2);

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
		enc = avcodec_find_encoder_by_name(kVideoEncoders[encIdx].vaapiEncoder);
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
					enc_ctx->time_base = av_inv_q(frameRate);
					enc_ctx->framerate = frameRate;
					enc_ctx->bit_rate = m_bitrate;
					enc_ctx->gop_size = gopSize;
					enc_ctx->max_b_frames = 0;
					enc_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);

					// No preset/tune here: those belong to the software
					// encoders and mean nothing to a VAAPI one.
					if ((ret = avcodec_open2(enc_ctx, enc, nullptr)) >= 0)
						hardware = true;
					else
						qWarning() << QString("The VAAPI %1 encoder would not open (%2), encoding in software")
						              .arg(QLatin1String(kVideoEncoders[encIdx].codec), ff_errstr(ret));
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
			qWarning() << QString("FFmpeg has no %1 encoder, encoding in software")
			              .arg(QLatin1String(kVideoEncoders[encIdx].vaapiEncoder));
		}

		if (!hardware)
		{
			if (enc_ctx) avcodec_free_context(&enc_ctx);
			if (hw_frames_ref) av_buffer_unref(&hw_frames_ref);
			enc = nullptr;
		}
	}
#endif

	// Named rather than taken from enc->name because the VAAPI branch above
	// may have left a different encoder in enc; filled in below for the
	// software path, which is the only one the private options apply to.
	QString swEncoderName;

	if (!hardware)
	{
		enc = avcodec_find_encoder_by_name(kVideoEncoders[encIdx].swEncoder);
		if (!enc)
			enc = avcodec_find_encoder(kVideoEncoders[encIdx].id);
		enc_ctx = enc ? avcodec_alloc_context3(enc) : nullptr;
		if (!enc || !enc_ctx)
		{
			QString msg = QString("No %1 encoder available").arg(QLatin1String(kVideoEncoders[encIdx].codec));
			qWarning() << msg;
			emit errorOccurred(msg);
			if (enc_ctx) avcodec_free_context(&enc_ctx);
			avcodec_free_context(&dec_ctx);
			return;
		}

		swEncoderName = QLatin1String(enc->name);

		enc_ctx->width = m_width;
		enc_ctx->height = m_height;
		enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
		enc_ctx->time_base = av_inv_q(frameRate);
		enc_ctx->framerate = frameRate;
		enc_ctx->bit_rate = m_bitrate;
		enc_ctx->gop_size = gopSize;
		// B-frames reorder output, which would cost latency in a live call.
		enc_ctx->max_b_frames = 0;

		// Note the absence of AV_CODEC_FLAG_GLOBAL_HEADER: we want the parameter
		// sets repeated in-band with every keyframe, since the receiving side has
		// no out-of-band way to learn them and may start decoding mid-stream.
		applyLiveEncoderOptions(enc_ctx, swEncoderName, kSliceMaxSize);

		if ((ret = avcodec_open2(enc_ctx, enc, nullptr)) < 0)
		{
			QString msg = QString("Could not open the %1 encoder: %2").arg(swEncoderName, ff_errstr(ret));
			qWarning() << msg;
			emit errorOccurred(msg);
			avcodec_free_context(&enc_ctx);
			avcodec_free_context(&dec_ctx);
			return;
		}
	}

	emit accelerationInUse(hardware
	                       ? QString("GPU encoding (VAAPI %1)").arg(QLatin1String(kVideoEncoders[encIdx].codec))
	                       : QString("CPU encoding (%1)").arg(swEncoderName));

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
					qWarning() << QString("%1 encode failed").arg(QLatin1String(avcodec_get_name(enc_ctx->codec_id)));
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

			// The codec is open and the loop is consuming, so the backlog held
			// for startup is no longer wanted: from here on the newest frame
			// wins. Set under the queue mutex so enqueueFrame() cannot read it
			// while this frame is being taken.
			m_decoderStarted.storeRelaxed(1);
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
				qWarning() << QString("%1 decode failed: %2").arg(m_codec, ff_errstr(ret));
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
