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
}

// Encoding defaults. Not exposed as settings yet - see the deferred list in
// the feature plan; a fixed, widely supported set of parameters for now.
static const int kFrameRate = 15;
static const int64_t kBitrate = 600000;
// Keyframe every ~2 seconds. There is no retransmission or keyframe request
// machinery yet, so this doubles as how quickly a receiver that missed data
// can resynchronize.
static const int kGopSize = kFrameRate * 2;

// Largest amount of encoded data one 0x77 message can carry: the protocol's
// per-message payload cap, minus the sequence number + timestamp header that
// every call media packet carries.
static const int kMaxFragmentSize = PICA_PROTO_C2CMSG_MAXDATASIZE - PICA_PROTO_CALL_PACKET_HDRSIZE;

static QString ff_errstr(int err)
{
	char buf[256] = {0};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromLocal8Bit(buf);
}

VideoFrameAssembler::VideoFrameAssembler()
	: m_timestamp(0), m_inProgress(false)
{
}

void VideoFrameAssembler::reset()
{
	m_buffer.clear();
	m_inProgress = false;
}

QByteArray VideoFrameAssembler::addFragment(quint16 seq_num, quint32 timestamp, const QByteArray &data)
{
	// Fragments of one encoded frame all carry the same timestamp; a
	// different one while a frame is still being assembled means the rest of
	// that frame is never coming, so drop what was collected and start over.
	if (m_inProgress && timestamp != m_timestamp)
		m_buffer.clear();

	m_buffer.append(data);
	m_timestamp = timestamp;
	m_inProgress = true;

	if (!(seq_num & LastFragmentFlag))
		return QByteArray();

	QByteArray frame = m_buffer;
	reset();

	return frame;
}

VideoDevice::VideoDevice(QObject *parent)
	: QObject(parent), m_width(640), m_height(480), m_abort(0)
{
}

VideoDevice::~VideoDevice()
{
	Close();
}

void VideoDevice::configureCapture(QString deviceName, QString codec, int width, int height)
{
	m_deviceName = deviceName;
	m_codec = codec;
	m_width = width;
	m_height = height;
}

void VideoDevice::configurePlayback(QString codec, int width, int height)
{
	m_codec = codec;
	m_width = width;
	m_height = height;
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

void VideoDevice::Capture()
{
	m_abort.storeRelaxed(0);

	avdevice_register_all();
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
	avcodec_register_all();
#endif

	const QByteArray deviceUtf8 = m_deviceName.toUtf8();
	const QByteArray driverUtf8 = VideoDevice::PlatformDriverName().toUtf8();
	const AVInputFormat *ifmt = av_find_input_format(driverUtf8.constData());
	if (!ifmt)
	{
		QString msg = QString("Could not find '%1' video input driver").arg(VideoDevice::PlatformDriverName());
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	AVDictionary *opts = nullptr;
	av_dict_set(&opts, "video_size", QString("%1x%2").arg(m_width).arg(m_height).toUtf8().constData(), 0);
	av_dict_set(&opts, "framerate", QString::number(kFrameRate).toUtf8().constData(), 0);

	AVFormatContext *ifmt_ctx = nullptr;
	int ret = avformat_open_input(&ifmt_ctx, deviceUtf8.constData(), ifmt, &opts);
	av_dict_free(&opts);
	if (ret < 0)
	{
		// Not fatal to the call - it simply proceeds without outgoing video.
		QString msg = QString("Could not open camera '%1': %2").arg(m_deviceName, ff_errstr(ret));
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

	// The camera hands us either raw frames or, on many webcams, an already
	// compressed stream (MJPEG). Either way it goes through a decoder here to
	// get raw pictures for the encoder. Forwarding an already compressed
	// camera stream untouched is a planned optimization, not done yet.
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
		avformat_close_input(&ifmt_ctx);
		return;
	}

	const AVCodec *enc = avcodec_find_encoder_by_name("libx264");
	if (!enc)
		enc = avcodec_find_encoder(AV_CODEC_ID_H264);
	AVCodecContext *enc_ctx = enc ? avcodec_alloc_context3(enc) : nullptr;
	if (!enc || !enc_ctx)
	{
		QString msg = "H.264 encoder not available";
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
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

	if ((ret = avcodec_open2(enc_ctx, enc, nullptr)) < 0)
	{
		QString msg = QString("Could not open H.264 encoder: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&enc_ctx);
		avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	AVFrame *enc_frame = av_frame_alloc();
	if (enc_frame)
	{
		enc_frame->format = enc_ctx->pix_fmt;
		enc_frame->width = enc_ctx->width;
		enc_frame->height = enc_ctx->height;
	}
	if (!enc_frame || av_frame_get_buffer(enc_frame, 0) < 0)
	{
		QString msg = "Could not allocate encoder frame";
		qWarning() << msg;
		emit errorOccurred(msg);
		if (enc_frame) av_frame_free(&enc_frame);
		avcodec_free_context(&enc_ctx);
		avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
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
				                     enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
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

			if (avcodec_send_frame(enc_ctx, enc_frame) != 0)
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
				// One encoded frame goes out as one or more 0x77 messages;
				// the final one is flagged so the receiver knows the frame is
				// complete (see the fragmentation notes in the protocol doc).
				int offset = 0;
				while (offset < out_pkt->size)
				{
					int chunk = qMin(kMaxFragmentSize, out_pkt->size - offset);
					bool isLast = (offset + chunk >= out_pkt->size);

					emit packetReady(QByteArray((const char *)out_pkt->data + offset, chunk), isLast);
					offset += chunk;
				}
				av_packet_unref(out_pkt);
			}
		}
	}

	av_packet_free(&in_pkt);
	av_packet_free(&out_pkt);
	av_frame_free(&cam_frame);
	av_frame_free(&enc_frame);
	if (sws) sws_freeContext(sws);
	avcodec_free_context(&enc_ctx);
	avcodec_free_context(&dec_ctx);
	avformat_close_input(&ifmt_ctx);
}

void VideoDevice::Play()
{
	m_abort.storeRelaxed(0);

#if LIBAVFORMAT_VERSION_MAJOR < 58
	avcodec_register_all();
#endif

	// Only "h264" is understood by this slice - matches what
	// AudioVideoCallController always negotiates via SendVideoParams().
	const AVCodec *dec = avcodec_find_decoder(AV_CODEC_ID_H264);
	AVCodecContext *dec_ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
	if (m_codec != QLatin1String("h264") || !dec || !dec_ctx || avcodec_open2(dec_ctx, dec, nullptr) < 0)
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
			// Rebuilt whenever the incoming picture format or size changes -
			// including the first frame, when they first become known.
			if (!sws || dec_frame->width != sws_width || dec_frame->height != sws_height ||
			    (AVPixelFormat)dec_frame->format != sws_fmt)
			{
				if (sws) sws_freeContext(sws);
				sws_width = dec_frame->width;
				sws_height = dec_frame->height;
				sws_fmt = (AVPixelFormat)dec_frame->format;

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
					av_frame_unref(dec_frame);
					continue;
				}
			}

			if (av_frame_make_writable(rgb_frame) < 0)
			{
				av_frame_unref(dec_frame);
				continue;
			}

			sws_scale(sws, dec_frame->data, dec_frame->linesize, 0, dec_frame->height,
			          rgb_frame->data, rgb_frame->linesize);

			// Deep copy: the QImage outlives this iteration (it travels to
			// the GUI thread through a queued signal), while rgb_frame's
			// buffer gets reused by the next scale.
			QImage img(rgb_frame->data[0], sws_width, sws_height,
			           rgb_frame->linesize[0], QImage::Format_RGB888);
			emit frameReady(img.copy());

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
