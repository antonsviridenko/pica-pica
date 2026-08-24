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
#include "audiodevice.h"
#include "nativeaudio.h"

#include <QDebug>
#include <QMutexLocker>
#include <QVector>
#include <cstring>

#ifdef Q_OS_LINUX
#include <alsa/asoundlib.h>
#endif

#ifdef HAVE_LIBPULSE
#include <pulse/pulseaudio.h>
#endif

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

// See toneplayer.cpp for the rationale - FFmpeg 5.1 (libavutil 57.24.100)
// replaced the channels/channel_layout fields with the AVChannelLayout
// ch_layout API; the legacy branch keeps us building on FFmpeg 4.x.
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#  define AD_HAVE_CH_LAYOUT 1
#else
#  define AD_HAVE_CH_LAYOUT 0
#endif

// FFmpeg 7.1 (libavcodec 61.13.100) added avcodec_get_supported_config() and
// deprecated the AVCodec fields listing what a codec supports; FFmpeg 9.0
// removed those fields. See chooseEncoderSampleFmt().
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
#  define AD_HAVE_SUPPORTED_CONFIG 1
#else
#  define AD_HAVE_SUPPORTED_CONFIG 0
#endif

// Every call this codebase currently negotiates is mono - matches
// TonePlayer's design and keeps the resampling/framing code below simple.
static const int kChannels = 1;

// Target bitrate for the Opus voice encoder. Not exposed as a setting yet;
// a reasonable fixed default for a single speech channel.
static const int64_t kOpusBitrate = 32000;

// The FFmpeg driver name used on Linux. Kept in a process wide variable
// rather than read from the settings database on demand, because the audio
// loops run on their own threads and QSqlDatabase connections belong to the
// thread that opened them. SetLinuxDriverName() is called from the GUI
// thread; see main.cpp and SettingsDialog::storeSettings().
static QMutex s_driverMutex;
static QString s_linuxDriver = QStringLiteral("alsa");

static QString ff_errstr(int err)
{
	char buf[256] = {0};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromLocal8Bit(buf);
}

static void applyMonoLayout(AVCodecContext *ctx)
{
#if AD_HAVE_CH_LAYOUT
	av_channel_layout_default(&ctx->ch_layout, kChannels);
#else
	ctx->channels = kChannels;
	ctx->channel_layout = av_get_default_channel_layout(kChannels);
#endif
}

static void applyMonoLayout(AVFrame *frame)
{
#if AD_HAVE_CH_LAYOUT
	av_channel_layout_default(&frame->ch_layout, kChannels);
#else
	frame->channels = kChannels;
	frame->channel_layout = av_get_default_channel_layout(kChannels);
#endif
}

static SwrContext *makeMonoResampler(AVSampleFormat inFmt, int inRate, AVSampleFormat outFmt, int outRate)
{
	SwrContext *swr = nullptr;

#if AD_HAVE_CH_LAYOUT
	AVChannelLayout layout;
	av_channel_layout_default(&layout, kChannels);
	int ret = swr_alloc_set_opts2(&swr, &layout, outFmt, outRate, &layout, inFmt, inRate, 0, nullptr);
	av_channel_layout_uninit(&layout);
	if (ret < 0)
		return nullptr;
#else
	swr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_MONO, outFmt, outRate, AV_CH_LAYOUT_MONO, inFmt, inRate, 0, nullptr);
	if (!swr)
		return nullptr;
#endif

	if (swr_init(swr) < 0)
	{
		swr_free(&swr);
		return nullptr;
	}

	return swr;
}

// Which sample format to run the Opus encoder in. Interleaved 16 bit is
// preferred over whatever the encoder lists first, because that is the format
// the echo canceller works in - taking it means the microphone samples can go
// through the canceller in place on their way to the encoder rather than
// being converted twice around it. libopus offers it; the native "opus"
// encoder does not, and on that one the AEC has to convert.
static AVSampleFormat chooseEncoderSampleFmt(const AVCodec *enc)
{
	if (!enc)
		return AV_SAMPLE_FMT_S16;

	const enum AVSampleFormat *fmts = nullptr;

#if AD_HAVE_SUPPORTED_CONFIG
	// FFmpeg 7.1 (libavcodec 61.13.100) replaced AVCodec's arrays of
	// supported formats with this query and deprecated the fields; 9.0
	// removed the fields outright, which is what MXE builds against.
	const void *configs = nullptr;
	if (avcodec_get_supported_config(nullptr, enc, AV_CODEC_CONFIG_SAMPLE_FORMAT,
	                                 0, &configs, nullptr) < 0)
		return AV_SAMPLE_FMT_S16;

	fmts = (const enum AVSampleFormat *)configs;
#else
	fmts = enc->sample_fmts;
#endif

	// No list at all means the encoder takes whatever it is given - so it
	// takes the one we would rather have.
	if (!fmts || fmts[0] == AV_SAMPLE_FMT_NONE)
		return AV_SAMPLE_FMT_S16;

	for (int i = 0; fmts[i] != AV_SAMPLE_FMT_NONE; i++)
	{
		if (fmts[i] == AV_SAMPLE_FMT_S16)
			return AV_SAMPLE_FMT_S16;
	}

	return fmts[0];
}

AudioDevice::AudioDevice(QObject *parent)
	: QObject(parent), m_sampleRate(48000), m_abort(0),
	  m_lastPlayedSeq(0), m_havePlayedSeq(false)
{
}

AudioDevice::~AudioDevice()
{
	Close();
}

void AudioDevice::configureCapture(QString deviceName, QString codec, int sampleRate)
{
	m_deviceName = deviceName;
	m_codec = codec;
	m_sampleRate = sampleRate;
}

void AudioDevice::configurePlayback(QString deviceName, QString codec, int sampleRate)
{
	m_deviceName = deviceName;
	m_codec = codec;
	m_sampleRate = sampleRate;
}

void AudioDevice::setEchoCanceller(EchoCancellerPtr ec)
{
	m_echoCanceller = ec;
}

// Difference between two sequence numbers, correct across the point where
// they wrap around: positive if a comes after b.
static inline int seq_diff(quint16 a, quint16 b)
{
	return (qint16)(a - b);
}

void AudioDevice::enqueuePacket(quint16 seq_num, QByteArray data)
{
	QMutexLocker locker(&m_queueMutex);
	int i;

	// Too late to be of any use - the audio it belongs to has already been
	// played. This also throws away duplicates of packets already played.
	if (m_havePlayedSeq && seq_diff(seq_num, m_lastPlayedSeq) <= 0)
		return;

	// Insertion point, and duplicates of packets still waiting.
	for (i = m_playQueue.size(); i > 0; i--)
	{
		int d = seq_diff(seq_num, m_playQueue[i - 1].seq);

		if (d == 0)
			return;

		if (d > 0)
			break;
	}

	while (m_playQueue.size() >= kMaxQueueDepth)
	{
		m_playQueue.removeFirst();

		if (i > 0)
			i--;
		else
			return; // the packet just dropped was this one's place
	}

	PlaybackPacket p = { seq_num, data };
	m_playQueue.insert(i, p);

	m_queueCond.wakeAll();
}

void AudioDevice::Close()
{
	m_abort.storeRelaxed(1);

	{
		QMutexLocker locker(&m_queueMutex);
		m_playQueue.clear();
		/* the next call starts its own numbering */
		m_havePlayedSeq = false;
	}
	m_queueCond.wakeAll();
}

void AudioDevice::waitForPreroll()
{
	// Preroll: wait for a small cushion of buffered packets before writing
	// the first one, so ordinary arrival jitter doesn't immediately drain
	// the queue and starve the device buffer (heard as crackling/xruns).
	QMutexLocker locker(&m_queueMutex);

	while (m_playQueue.size() < kPrerollDepth && !m_abort.loadRelaxed())
		m_queueCond.wait(&m_queueMutex, 100);
}

bool AudioDevice::nextPacket(QByteArray *out)
{
	QMutexLocker locker(&m_queueMutex);

	while (m_playQueue.isEmpty() && !m_abort.loadRelaxed())
		m_queueCond.wait(&m_queueMutex, 100);

	if (m_playQueue.isEmpty())
		return !m_abort.loadRelaxed();

	PlaybackPacket p = m_playQueue.takeFirst();

	*out = p.data;
	m_lastPlayedSeq = p.seq;
	m_havePlayedSeq = true;

	return true;
}

void AudioDevice::Capture()
{
	m_abort.storeRelaxed(0);

	const QString driver = AudioDevice::PlatformDriverName(CAPTURE);

	if (AudioDevice::IsPlatformDriver(driver))
		captureNative(AudioDevice::PlatformApiName(driver));
	else
		captureFFmpeg(driver);
}

void AudioDevice::Play()
{
	m_abort.storeRelaxed(0);

	const QString driver = AudioDevice::PlatformDriverName(PLAYBACK);

	if (AudioDevice::IsPlatformDriver(driver))
		playNative(AudioDevice::PlatformApiName(driver));
	else
		playFFmpeg(driver);
}

void AudioDevice::captureFFmpeg(const QString &driver)
{
	avdevice_register_all();
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
	avcodec_register_all();
#endif

	const QByteArray deviceUtf8 = m_deviceName.toUtf8();
	const QByteArray driverUtf8 = driver.toUtf8();
	const AVInputFormat *ifmt = av_find_input_format(driverUtf8.constData());
	if (!ifmt)
	{
		QString msg = QString("Could not find '%1' input driver").arg(driver);
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	AVDictionary *opts = nullptr;
	av_dict_set(&opts, "sample_rate", QString::number(m_sampleRate).toUtf8().constData(), 0);
	av_dict_set(&opts, "channels", QString::number(kChannels).toUtf8().constData(), 0);

	AVFormatContext *ifmt_ctx = nullptr;
	int ret = avformat_open_input(&ifmt_ctx, deviceUtf8.constData(), ifmt, &opts);
	av_dict_free(&opts);
	if (ret < 0)
	{
		QString msg = QString("Could not open capture device '%1': %2").arg(m_deviceName, ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, nullptr)) < 0 || ifmt_ctx->nb_streams < 1)
	{
		QString msg = QString("Could not determine capture stream parameters: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	AVStream *in_st = ifmt_ctx->streams[0];

	emit deviceFormatInUse(QString("%1 in %2 Hz %3 ch %4 -> %5 Hz %6 ch")
	                       .arg(driver)
	                       .arg(in_st->codecpar->sample_rate)
#if AD_HAVE_CH_LAYOUT
	                       .arg(in_st->codecpar->ch_layout.nb_channels)
#else
	                       .arg(in_st->codecpar->channels)
#endif
	                       .arg(QLatin1String(avcodec_get_name(in_st->codecpar->codec_id)))
	                       .arg(m_sampleRate).arg(kChannels));

	const AVCodec *pcm_dec = avcodec_find_decoder(in_st->codecpar->codec_id);
	AVCodecContext *dec_ctx = pcm_dec ? avcodec_alloc_context3(pcm_dec) : nullptr;
	if (!pcm_dec || !dec_ctx ||
	    avcodec_parameters_to_context(dec_ctx, in_st->codecpar) < 0 ||
	    avcodec_open2(dec_ctx, pcm_dec, nullptr) < 0)
	{
		QString msg = "Could not open capture format decoder";
		qWarning() << msg;
		emit errorOccurred(msg);
		if (dec_ctx) avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	const AVCodec *enc = avcodec_find_encoder_by_name("libopus");
	if (!enc)
		enc = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	AVCodecContext *enc_ctx = enc ? avcodec_alloc_context3(enc) : nullptr;
	if (!enc || !enc_ctx)
	{
		QString msg = "Opus encoder not available";
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	enc_ctx->sample_rate = m_sampleRate;
	enc_ctx->sample_fmt = chooseEncoderSampleFmt(enc);
	enc_ctx->bit_rate = kOpusBitrate;
	enc_ctx->time_base = AVRational{1, m_sampleRate};
	applyMonoLayout(enc_ctx);

	// "voip" favors speech intelligibility over faithfulness and has lower
	// algorithmic delay than the default "audio" mode - a better fit for a
	// live call than for e.g. music. Only libopus exposes this private
	// option; harmless no-op on the native "opus" encoder fallback.
	av_opt_set(enc_ctx->priv_data, "application", "voip", 0);

	if ((ret = avcodec_open2(enc_ctx, enc, nullptr)) < 0)
	{
		QString msg = QString("Could not open Opus encoder: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&enc_ctx);
		avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	SwrContext *swr = makeMonoResampler(dec_ctx->sample_fmt, dec_ctx->sample_rate, enc_ctx->sample_fmt, enc_ctx->sample_rate);
	if (!swr)
	{
		QString msg = "Could not set up capture resampler";
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&enc_ctx);
		avcodec_free_context(&dec_ctx);
		avformat_close_input(&ifmt_ctx);
		return;
	}

	AVAudioFifo *fifo = av_audio_fifo_alloc(enc_ctx->sample_fmt, kChannels, 1);
	int frame_size = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 960;

	// The canceller only gets to work in place when the encoder is taking
	// interleaved 16 bit, which libopus does. On the native encoder, whose
	// only format is planar float, cancelling would mean converting to 16 bit
	// and back around every frame; not worth it for a fallback path.
	EchoCancellerPtr ec = m_echoCanceller;
	if (ec && (!ec->isValid() || enc_ctx->sample_fmt != AV_SAMPLE_FMT_S16 ||
	           ec->sampleRate() != m_sampleRate))
	{
		if (ec->isValid())
			qWarning() << "Echo cancellation disabled: encoder format" << av_get_sample_fmt_name(enc_ctx->sample_fmt)
			           << "at" << m_sampleRate << "Hz is not what the canceller was built for";
		ec.clear();
	}

	AVPacket *in_pkt = av_packet_alloc();
	AVFrame *dec_frame = av_frame_alloc();
	AVPacket *out_pkt = av_packet_alloc();
	int64_t pts = 0;
	bool loggedDecodeError = false;
	bool loggedEncodeError = false;

	while (!m_abort.loadRelaxed())
	{
		ret = av_read_frame(ifmt_ctx, in_pkt);
		if (ret < 0)
		{
			if (ret == AVERROR(EAGAIN))
				continue;
			if (!loggedDecodeError)
			{
				qWarning() << QString("Capture device read failed: %1").arg(ff_errstr(ret));
				loggedDecodeError = true;
			}
			break;
		}

		ret = avcodec_send_packet(dec_ctx, in_pkt);
		av_packet_unref(in_pkt);
		if (ret < 0)
		{
			if (!loggedDecodeError)
			{
				qWarning() << QString("Capture format decode failed: %1").arg(ff_errstr(ret));
				loggedDecodeError = true;
			}
			continue;
		}

		while (avcodec_receive_frame(dec_ctx, dec_frame) == 0)
		{
			uint8_t **converted = nullptr;
			int out_samples = (int)av_rescale_rnd(swr_get_delay(swr, dec_frame->sample_rate) + dec_frame->nb_samples,
			                                       enc_ctx->sample_rate, dec_frame->sample_rate, AV_ROUND_UP);

			if (out_samples > 0 &&
			    av_samples_alloc_array_and_samples(&converted, nullptr, kChannels, out_samples, enc_ctx->sample_fmt, 0) >= 0)
			{
				int converted_samples = swr_convert(swr, converted, out_samples,
				                                     (const uint8_t **)dec_frame->data, dec_frame->nb_samples);

				if (converted_samples > 0 &&
				    av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + converted_samples) >= 0)
				{
					av_audio_fifo_write(fifo, (void **)converted, converted_samples);
				}

				av_freep(&converted[0]);
				av_freep(&converted);
			}
			av_frame_unref(dec_frame);

			while (av_audio_fifo_size(fifo) >= frame_size)
			{
				AVFrame *enc_frame = av_frame_alloc();
				enc_frame->format = enc_ctx->sample_fmt;
				enc_frame->sample_rate = enc_ctx->sample_rate;
				enc_frame->nb_samples = frame_size;
				applyMonoLayout(enc_frame);
				av_frame_get_buffer(enc_frame, 0);

				av_audio_fifo_read(fifo, (void **)enc_frame->data, frame_size);

				// Take the echo of what the playback direction has been
				// feeding the speaker back out of the microphone signal,
				// before it is encoded and sent.
				if (ec)
					ec->processNearEnd((qint16 *)enc_frame->data[0], frame_size);

				enc_frame->pts = pts;
				pts += frame_size;

				if (avcodec_send_frame(enc_ctx, enc_frame) == 0)
				{
					while (avcodec_receive_packet(enc_ctx, out_pkt) == 0)
					{
						emit packetReady(QByteArray((const char *)out_pkt->data, out_pkt->size));
						av_packet_unref(out_pkt);
					}
				}
				else if (!loggedEncodeError)
				{
					qWarning() << "Opus encode failed";
					loggedEncodeError = true;
				}
				av_frame_free(&enc_frame);
			}
		}
	}

	av_packet_free(&in_pkt);
	av_packet_free(&out_pkt);
	av_frame_free(&dec_frame);
	av_audio_fifo_free(fifo);
	swr_free(&swr);
	avcodec_free_context(&enc_ctx);
	avcodec_free_context(&dec_ctx);
	avformat_close_input(&ifmt_ctx);
}

void AudioDevice::captureNative(const QString &api)
{
	NativeAudioBackend *backend = NativeAudioBackend::create(api);
	if (!backend)
	{
		QString msg = QString("No '%1' audio backend in this build").arg(api);
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	QString err;
	if (!backend->openCapture(m_deviceName, m_sampleRate, kChannels, NativeAudioVoiceCall, &err))
	{
		qWarning() << err;
		emit errorOccurred(err);
		delete backend;
		return;
	}

	emit deviceFormatInUse(backend->formatDescription());

	const AVCodec *enc = avcodec_find_encoder_by_name("libopus");
	if (!enc)
		enc = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	AVCodecContext *enc_ctx = enc ? avcodec_alloc_context3(enc) : nullptr;
	if (!enc || !enc_ctx)
	{
		QString msg = "Opus encoder not available";
		qWarning() << msg;
		emit errorOccurred(msg);
		if (enc_ctx) avcodec_free_context(&enc_ctx);
		backend->close();
		delete backend;
		return;
	}

	enc_ctx->sample_rate = m_sampleRate;
	enc_ctx->sample_fmt = chooseEncoderSampleFmt(enc);
	enc_ctx->bit_rate = kOpusBitrate;
	enc_ctx->time_base = AVRational{1, m_sampleRate};
	applyMonoLayout(enc_ctx);
	av_opt_set(enc_ctx->priv_data, "application", "voip", 0);

	int ret = avcodec_open2(enc_ctx, enc, nullptr);
	if (ret < 0)
	{
		QString msg = QString("Could not open Opus encoder: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&enc_ctx);
		backend->close();
		delete backend;
		return;
	}

	// The backend delivers interleaved 16 bit at our rate. Anything but an
	// encoder that takes the same needs a converter in between.
	SwrContext *swr = nullptr;
	if (enc_ctx->sample_fmt != AV_SAMPLE_FMT_S16)
	{
		swr = makeMonoResampler(AV_SAMPLE_FMT_S16, m_sampleRate, enc_ctx->sample_fmt, m_sampleRate);
		if (!swr)
		{
			QString msg = "Could not set up capture converter";
			qWarning() << msg;
			emit errorOccurred(msg);
			avcodec_free_context(&enc_ctx);
			backend->close();
			delete backend;
			return;
		}
	}

	// Nothing for us to do when the platform has already taken the echo out
	// below us - running a second canceller over an already cancelled signal
	// only gives it a near silent reference to chase and can chew holes in
	// the speech.
	EchoCancellerPtr ec = m_echoCanceller;
	if (backend->hasPlatformEchoCancellation())
	{
		if (ec)
			qDebug() << api << "provides echo cancellation - not running ours as well";
		ec.clear();
	}
	else if (ec && (!ec->isValid() || ec->sampleRate() != m_sampleRate))
	{
		ec.clear();
	}

	const int frame_size = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 960;

	QVector<qint16> pending;
	QVector<qint16> scratch(frame_size * 4);
	QVector<qint16> block(frame_size);

	AVPacket *out_pkt = av_packet_alloc();
	int64_t pts = 0;
	bool loggedEncodeError = false;

	while (!m_abort.loadRelaxed())
	{
		int got = backend->read(scratch.data(), scratch.size(), 100);
		if (got < 0)
		{
			qWarning() << "Native capture read failed";
			break;
		}
		if (got == 0)
			continue;

		int base = pending.size();
		pending.resize(base + got);
		memcpy(pending.data() + base, scratch.constData(), got * sizeof(qint16));

		int consumed = 0;
		while (pending.size() - consumed >= frame_size)
		{
			memcpy(block.data(), pending.constData() + consumed, frame_size * sizeof(qint16));
			consumed += frame_size;

			if (ec)
				ec->processNearEnd(block.data(), frame_size);

			AVFrame *enc_frame = av_frame_alloc();
			enc_frame->format = enc_ctx->sample_fmt;
			enc_frame->sample_rate = enc_ctx->sample_rate;
			enc_frame->nb_samples = frame_size;
			applyMonoLayout(enc_frame);

			if (av_frame_get_buffer(enc_frame, 0) < 0)
			{
				av_frame_free(&enc_frame);
				continue;
			}

			if (swr)
			{
				const uint8_t *in[1] = { (const uint8_t *)block.constData() };
				if (swr_convert(swr, enc_frame->data, frame_size, in, frame_size) <= 0)
				{
					av_frame_free(&enc_frame);
					continue;
				}
			}
			else
			{
				memcpy(enc_frame->data[0], block.constData(), frame_size * sizeof(qint16));
			}

			enc_frame->pts = pts;
			pts += frame_size;

			if (avcodec_send_frame(enc_ctx, enc_frame) == 0)
			{
				while (avcodec_receive_packet(enc_ctx, out_pkt) == 0)
				{
					emit packetReady(QByteArray((const char *)out_pkt->data, out_pkt->size));
					av_packet_unref(out_pkt);
				}
			}
			else if (!loggedEncodeError)
			{
				qWarning() << "Opus encode failed";
				loggedEncodeError = true;
			}

			av_frame_free(&enc_frame);
		}

		if (consumed > 0)
			pending.remove(0, consumed);
	}

	av_packet_free(&out_pkt);
	if (swr) swr_free(&swr);
	avcodec_free_context(&enc_ctx);
	backend->close();
	delete backend;
}

void AudioDevice::playFFmpeg(const QString &driver)
{
	avdevice_register_all();
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
	avcodec_register_all();
#endif

	AVFormatContext *oc = nullptr;
	const QByteArray deviceUtf8 = m_deviceName.toUtf8();
	const QByteArray driverUtf8 = driver.toUtf8();

	int ret = avformat_alloc_output_context2(&oc, nullptr, driverUtf8.constData(), deviceUtf8.constData());
	if (ret < 0 || !oc)
	{
		QString msg = QString("Could not allocate '%1' output context: %2").arg(driver, ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	const AVCodec *out_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
	AVCodecContext *out_ctx = out_codec ? avcodec_alloc_context3(out_codec) : nullptr;
	if (!out_codec || !out_ctx)
	{
		QString msg = "PCM encoder not found";
		qWarning() << msg;
		emit errorOccurred(msg);
		avformat_free_context(oc);
		return;
	}

	out_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
	out_ctx->sample_rate = m_sampleRate;
	out_ctx->time_base = AVRational{1, m_sampleRate};
	applyMonoLayout(out_ctx);

	if ((ret = avcodec_open2(out_ctx, out_codec, nullptr)) < 0)
	{
		QString msg = QString("Could not open playback codec: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&out_ctx);
		avformat_free_context(oc);
		return;
	}

	AVStream *st = avformat_new_stream(oc, nullptr);
	if (!st || avcodec_parameters_from_context(st->codecpar, out_ctx) < 0)
	{
		QString msg = "Could not create playback stream";
		qWarning() << msg;
		emit errorOccurred(msg);
		avcodec_free_context(&out_ctx);
		avformat_free_context(oc);
		return;
	}
	st->time_base = out_ctx->time_base;

	if (!(oc->oformat->flags & AVFMT_NOFILE))
	{
		if ((ret = avio_open(&oc->pb, deviceUtf8.constData(), AVIO_FLAG_WRITE)) < 0)
		{
			QString msg = QString("Could not open '%1' device '%2': %3").arg(driver, m_deviceName, ff_errstr(ret));
			qWarning() << msg;
			emit errorOccurred(msg);
			avcodec_free_context(&out_ctx);
			avformat_free_context(oc);
			return;
		}
	}

	if ((ret = avformat_write_header(oc, nullptr)) < 0)
	{
		QString msg = QString("Error writing playback header: %1").arg(ff_errstr(ret));
		qWarning() << msg;
		emit errorOccurred(msg);
		if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
		avcodec_free_context(&out_ctx);
		avformat_free_context(oc);
		return;
	}

	// Only "opus" is understood by this slice - matches what
	// AudioVideoCallController always negotiates via SendAudioParams().
	const AVCodec *in_codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
	AVCodecContext *dec_ctx = in_codec ? avcodec_alloc_context3(in_codec) : nullptr;
	if (dec_ctx)
	{
		dec_ctx->sample_rate = m_sampleRate;
		applyMonoLayout(dec_ctx);
	}
	if (m_codec != QLatin1String("opus") || !in_codec || !dec_ctx || avcodec_open2(dec_ctx, in_codec, nullptr) < 0)
	{
		QString msg = QString("Unsupported call audio codec: %1").arg(m_codec);
		qWarning() << msg;
		emit errorOccurred(msg);
		if (dec_ctx) avcodec_free_context(&dec_ctx);
		if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
		avcodec_free_context(&out_ctx);
		avformat_free_context(oc);
		return;
	}

	EchoCancellerPtr ec = m_echoCanceller;
	if (ec && (!ec->isValid() || ec->sampleRate() != m_sampleRate))
		ec.clear();

	SwrContext *swr = nullptr;
	AVPacket *out_pkt = av_packet_alloc();
	AVFrame *dec_frame = av_frame_alloc();
	int64_t pts = 0;
	bool loggedDecodeError = false;
	bool loggedResamplerError = false;
	bool loggedEncodeError = false;

	waitForPreroll();

	while (true)
	{
		QByteArray pktData;
		if (!nextPacket(&pktData))
			break;
		if (pktData.isEmpty())
			continue;

		AVPacket *in_pkt = av_packet_alloc();
		if (av_new_packet(in_pkt, pktData.size()) < 0)
		{
			av_packet_free(&in_pkt);
			continue;
		}
		memcpy(in_pkt->data, pktData.constData(), pktData.size());

		ret = avcodec_send_packet(dec_ctx, in_pkt);
		av_packet_free(&in_pkt);
		if (ret < 0)
		{
			if (!loggedDecodeError)
			{
				qWarning() << QString("Opus decode failed: %1").arg(ff_errstr(ret));
				loggedDecodeError = true;
			}
			continue;
		}

		while (avcodec_receive_frame(dec_ctx, dec_frame) == 0)
		{
			if (!swr)
			{
				swr = makeMonoResampler((AVSampleFormat)dec_frame->format, dec_frame->sample_rate, out_ctx->sample_fmt, out_ctx->sample_rate);
				if (!swr)
				{
					if (!loggedResamplerError)
					{
						qWarning() << "Could not set up playback resampler";
						loggedResamplerError = true;
					}
					av_frame_unref(dec_frame);
					continue;
				}
			}

			int out_samples = (int)av_rescale_rnd(swr_get_delay(swr, dec_frame->sample_rate) + dec_frame->nb_samples,
			                                       out_ctx->sample_rate, dec_frame->sample_rate, AV_ROUND_UP);

			AVFrame *out_frame = av_frame_alloc();
			out_frame->format = out_ctx->sample_fmt;
			out_frame->sample_rate = out_ctx->sample_rate;
			out_frame->nb_samples = out_samples;
			applyMonoLayout(out_frame);

			if (out_samples > 0 && av_frame_get_buffer(out_frame, 0) == 0)
			{
				int converted_samples = swr_convert(swr, out_frame->data, out_samples,
				                                     (const uint8_t **)dec_frame->data, dec_frame->nb_samples);
				if (converted_samples > 0)
				{
					out_frame->nb_samples = converted_samples;
					out_frame->pts = pts;
					pts += converted_samples;

					// Hand the canceller the far end signal at the same
					// moment the sound card gets it, so the delay it has to
					// model is only the device buffer plus the room.
					if (ec)
						ec->pushFarEnd((const qint16 *)out_frame->data[0], converted_samples);

					if (avcodec_send_frame(out_ctx, out_frame) == 0)
					{
						while (avcodec_receive_packet(out_ctx, out_pkt) == 0)
						{
							out_pkt->stream_index = st->index;
							av_interleaved_write_frame(oc, out_pkt);
							av_packet_unref(out_pkt);
						}
					}
					else if (!loggedEncodeError)
					{
						qWarning() << "Playback PCM encode failed";
						loggedEncodeError = true;
					}
				}
			}
			av_frame_free(&out_frame);
			av_frame_unref(dec_frame);
		}
	}

	av_packet_free(&out_pkt);
	av_frame_free(&dec_frame);
	if (swr) swr_free(&swr);
	avcodec_free_context(&dec_ctx);

	av_write_trailer(oc);
	if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
	avcodec_free_context(&out_ctx);
	avformat_free_context(oc);
}

void AudioDevice::playNative(const QString &api)
{
	NativeAudioBackend *backend = NativeAudioBackend::create(api);
	if (!backend)
	{
		QString msg = QString("No '%1' audio backend in this build").arg(api);
		qWarning() << msg;
		emit errorOccurred(msg);
		return;
	}

	QString err;
	if (!backend->openPlayback(m_deviceName, m_sampleRate, kChannels, NativeAudioVoiceCall, &err))
	{
		qWarning() << err;
		emit errorOccurred(err);
		delete backend;
		return;
	}

	emit deviceFormatInUse(backend->formatDescription());

	const AVCodec *in_codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
	AVCodecContext *dec_ctx = in_codec ? avcodec_alloc_context3(in_codec) : nullptr;
	if (dec_ctx)
	{
		dec_ctx->sample_rate = m_sampleRate;
		applyMonoLayout(dec_ctx);
	}
	if (m_codec != QLatin1String("opus") || !in_codec || !dec_ctx || avcodec_open2(dec_ctx, in_codec, nullptr) < 0)
	{
		QString msg = QString("Unsupported call audio codec: %1").arg(m_codec);
		qWarning() << msg;
		emit errorOccurred(msg);
		if (dec_ctx) avcodec_free_context(&dec_ctx);
		backend->close();
		delete backend;
		return;
	}

	// On a platform that cancels for us, the far end signal it needs is the
	// one it is rendering itself - there is nothing to hand to ours, and
	// nothing for ours to do.
	EchoCancellerPtr ec = m_echoCanceller;
	if (ec && (!ec->isValid() || ec->sampleRate() != m_sampleRate))
		ec.clear();

	SwrContext *swr = nullptr;
	AVFrame *dec_frame = av_frame_alloc();
	QVector<qint16> out;
	bool loggedDecodeError = false;
	bool loggedResamplerError = false;

	waitForPreroll();

	while (true)
	{
		QByteArray pktData;
		if (!nextPacket(&pktData))
			break;
		if (pktData.isEmpty())
			continue;

		AVPacket *in_pkt = av_packet_alloc();
		if (av_new_packet(in_pkt, pktData.size()) < 0)
		{
			av_packet_free(&in_pkt);
			continue;
		}
		memcpy(in_pkt->data, pktData.constData(), pktData.size());

		int ret = avcodec_send_packet(dec_ctx, in_pkt);
		av_packet_free(&in_pkt);
		if (ret < 0)
		{
			if (!loggedDecodeError)
			{
				qWarning() << QString("Opus decode failed: %1").arg(ff_errstr(ret));
				loggedDecodeError = true;
			}
			continue;
		}

		while (avcodec_receive_frame(dec_ctx, dec_frame) == 0)
		{
			if (!swr)
			{
				swr = makeMonoResampler((AVSampleFormat)dec_frame->format, dec_frame->sample_rate,
				                        AV_SAMPLE_FMT_S16, m_sampleRate);
				if (!swr)
				{
					if (!loggedResamplerError)
					{
						qWarning() << "Could not set up playback resampler";
						loggedResamplerError = true;
					}
					av_frame_unref(dec_frame);
					continue;
				}
			}

			int out_samples = (int)av_rescale_rnd(swr_get_delay(swr, dec_frame->sample_rate) + dec_frame->nb_samples,
			                                       m_sampleRate, dec_frame->sample_rate, AV_ROUND_UP);
			if (out_samples > 0)
			{
				out.resize(out_samples * kChannels);

				uint8_t *outPlanes[1] = { (uint8_t *)out.data() };
				int converted = swr_convert(swr, outPlanes, out_samples,
				                            (const uint8_t **)dec_frame->data, dec_frame->nb_samples);
				if (converted > 0)
				{
					if (ec)
						ec->pushFarEnd(out.constData(), converted * kChannels);

					if (backend->write(out.constData(), converted * kChannels, 100) < 0)
					{
						qWarning() << "Native playback write failed";
						av_frame_unref(dec_frame);
						goto done;
					}
				}
			}

			av_frame_unref(dec_frame);
		}
	}

done:
	av_frame_free(&dec_frame);
	if (swr) swr_free(&swr);
	avcodec_free_context(&dec_ctx);
	backend->close();
	delete backend;
}

#ifdef HAVE_LIBPULSE

// PulseAudio's introspection API is asynchronous, so a one-shot enumeration
// means running a private mainloop until the callbacks below have said they
// are done. Doing it on the caller's thread with its own context keeps this
// self contained - nothing else in pica-client talks to PulseAudio.
struct PulseEnumState
{
	QList<MediaDeviceInfo> *result;
	int *index;
	// The introspection callback has reported the end of the list, so
	// whatever happens to the connection afterwards is just teardown.
	bool completed;
	bool done;
	bool failed;
};

static void pulse_source_cb(pa_context *, const pa_source_info *info, int eol, void *userdata)
{
	PulseEnumState *st = (PulseEnumState *)userdata;

	if (eol)
	{
		st->completed = true;
		st->done = true;
		return;
	}

	if (!info)
		return;

	// Monitor sources are the loopback of an output, not a microphone; they
	// would show up in the capture list as duplicates of every speaker.
	if (info->monitor_of_sink != PA_INVALID_INDEX)
		return;

	MediaDeviceInfo d;
	d.device = QString::fromUtf8(info->name);
	d.humanReadable = QString::fromUtf8(info->description);
	d.index = (*st->index)++;
	*st->result << d;
}

static void pulse_sink_cb(pa_context *, const pa_sink_info *info, int eol, void *userdata)
{
	PulseEnumState *st = (PulseEnumState *)userdata;

	if (eol)
	{
		st->completed = true;
		st->done = true;
		return;
	}

	if (!info)
		return;

	MediaDeviceInfo d;
	d.device = QString::fromUtf8(info->name);
	d.humanReadable = QString::fromUtf8(info->description);
	d.index = (*st->index)++;
	*st->result << d;
}

static void pulse_state_cb(pa_context *ctx, void *userdata)
{
	PulseEnumState *st = (PulseEnumState *)userdata;

	// READY is handled by the loop in enumeratePulse(), which is where the
	// introspection request gets issued; only the ways this can end early
	// need noticing here.
	switch (pa_context_get_state(ctx))
	{
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		if (!st->completed)
			st->failed = true;
		st->done = true;
		break;

	default:
		break;
	}
}

static QList<MediaDeviceInfo> enumeratePulse(enum MediaDeviceStreamDirection dir)
{
	QList<MediaDeviceInfo> result;
	int index = 0;

	// PulseAudio's own idea of the default, which follows the user moving
	// their default device in the desktop's sound settings.
	{
		MediaDeviceInfo d;
		d.device = QStringLiteral("default");
		d.humanReadable = (dir == PLAYBACK)
		                  ? QObject::tr("Default output (PulseAudio)")
		                  : QObject::tr("Default input (PulseAudio)");
		d.index = index++;
		result << d;
	}

	pa_mainloop *mainloop = pa_mainloop_new();
	if (!mainloop)
		return result;

	pa_context *ctx = pa_context_new(pa_mainloop_get_api(mainloop), "pica-client");
	if (!ctx)
	{
		pa_mainloop_free(mainloop);
		return result;
	}

	PulseEnumState st;
	st.result = &result;
	st.index = &index;
	st.completed = false;
	st.done = false;
	st.failed = false;

	pa_context_set_state_callback(ctx, pulse_state_cb, &st);

	if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
	{
		pa_context_unref(ctx);
		pa_mainloop_free(mainloop);
		return result;
	}

	pa_operation *op = nullptr;
	bool requested = false;

	// Bounded so a server that never answers cannot hang the settings dialog.
	// One iteration is at most the 50ms below, so this is a 5 second ceiling.
	for (int iterations = 0; iterations < 100 && !st.done; iterations++)
	{
		if (pa_mainloop_prepare(mainloop, 50 * 1000) < 0)
			break;
		if (pa_mainloop_poll(mainloop) < 0)
			break;
		if (pa_mainloop_dispatch(mainloop) < 0)
			break;

		if (!requested && pa_context_get_state(ctx) == PA_CONTEXT_READY)
		{
			if (dir == PLAYBACK)
				op = pa_context_get_sink_info_list(ctx, pulse_sink_cb, &st);
			else
				op = pa_context_get_source_info_list(ctx, pulse_source_cb, &st);

			requested = true;

			if (!op)
				break;
		}
	}

	if (op)
		pa_operation_unref(op);

	pa_context_disconnect(ctx);
	pa_context_unref(ctx);
	pa_mainloop_free(mainloop);

	if (st.failed)
		qWarning() << "Could not enumerate PulseAudio devices - is a sound server running?";

	return result;
}

#endif // HAVE_LIBPULSE

#ifdef Q_OS_LINUX

static QList<MediaDeviceInfo> enumerateAlsa(enum MediaDeviceStreamDirection dir)
{
	QList<MediaDeviceInfo> result;
	int index = 0;

	void **hints, **n;
	char *name = NULL, *descr = NULL, *io = NULL;
	const char *direction = (dir == PLAYBACK) ? "Output" : "Input";

	if (snd_device_name_hint(-1, "pcm", &hints) < 0)
		return result;
	n = hints;
	while(*n)
	{
		MediaDeviceInfo d;

		name = snd_device_name_get_hint(*n, "NAME");
		descr = snd_device_name_get_hint(*n, "DESC");
		io = snd_device_name_get_hint(*n, "IOID");

		if (io && strcmp(io, direction))
			goto release_resources;

		d.device = QLatin1String(name);
		d.humanReadable = QLatin1String(descr);
		d.index = index++;
		if (strstr(name, "default") == name)
			result.prepend(d);
		else
			result << d;

	release_resources:
		free(io);
		free(descr);
		free(name);
		n++;
	}
	snd_device_name_free_hint(hints);

	return result;
}

#endif // Q_OS_LINUX

QList<MediaDeviceInfo> AudioDevice::Enumerate(enum MediaDeviceStreamDirection dir)
{
	const QString driver = AudioDevice::PlatformDriverName(dir);

	if (AudioDevice::IsPlatformDriver(driver))
		return NativeAudioBackend::enumerate(AudioDevice::PlatformApiName(driver), dir);

#ifdef HAVE_LIBPULSE
	if (driver == QLatin1String("pulse"))
		return enumeratePulse(dir);
#endif

#ifdef Q_OS_LINUX
	if (driver == QLatin1String("alsa"))
		return enumerateAlsa(dir);
#endif

	// Anything else - "oss", or "pulse" in a build without libpulse - has no
	// enumeration of its own worth writing, so the caller gets the one name
	// that always works.
	QList<MediaDeviceInfo> result;
	MediaDeviceInfo d;
	d.device = QStringLiteral("default");
	d.humanReadable = QObject::tr("Default device");
	d.index = 0;
	result << d;

	return result;
}

void AudioDevice::SetLinuxDriverName(const QString &driver)
{
	QMutexLocker locker(&s_driverMutex);

	if (driver == QLatin1String("pulse"))
		s_linuxDriver = QStringLiteral("pulse");
	else
		s_linuxDriver = QStringLiteral("alsa");
}

QString AudioDevice::LinuxDriverName()
{
	QMutexLocker locker(&s_driverMutex);
	return s_linuxDriver;
}

QString AudioDevice::PlatformDriverName(enum MediaDeviceStreamDirection dir)
{
	Q_UNUSED(dir)

#if defined(Q_OS_LINUX)
	// Both directions come from the same setting. ALSA is the default because
	// it is the one interface guaranteed to be there; "pulse" is worth
	// choosing on a desktop, where it (or PipeWire wearing its face) is what
	// actually owns the sound card, and where module-echo-cancel can take the
	// job off our hands entirely.
	return AudioDevice::LinuxDriverName();
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	// Not libavdevice's "audiotoolbox". CoreAudio's echo cancellation lives
	// in the VoiceProcessingIO audio unit, which renders and captures
	// together, and there is no way to reach it through a demuxer and a muxer
	// that know nothing about each other.
	return QStringLiteral("platform-coreaudio");
#elif defined(Q_OS_WIN)
	// DirectSound is a compatibility shim over
	// WASAPI these days and cannot mark a stream as belonging to a call,
	// which is the only way to get Windows' own echo cancellation.
	return QStringLiteral("platform-wasapi");
#else
	return QStringLiteral("oss");
#endif
}

bool AudioDevice::IsPlatformDriver(const QString &driver)
{
	return driver.startsWith(QLatin1String("platform-"));
}

QString AudioDevice::PlatformApiName(const QString &driver)
{
	if (!IsPlatformDriver(driver))
		return QString();

	return driver.mid(sizeof("platform-") - 1);
}
