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
#include "toneplayer.h"
#include "../audiodevice.h"

#include <QDebug>
#include <cmath>

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
}

// FFmpeg 5.1 (libavutil 57.24.100) replaced AVCodecContext/AVFrame's
// channels + channel_layout fields with the AVChannelLayout ch_layout API.
// The old fields were deprecated then and removed outright in FFmpeg 7.0.
// The legacy branch keeps us building on FFmpeg 4.x (Ubuntu 20.04 / 22.04).
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#  define TP_HAVE_CH_LAYOUT 1
#else
#  define TP_HAVE_CH_LAYOUT 0
#endif

TonePlayer::TonePlayer(QObject *parent)
    : QObject(parent),
      m_abort(0),
      m_deviceName("default"),
      m_sampleRate(48000),
      m_channels(1), // mono
      m_volume(0.6)
{
}

TonePlayer::~TonePlayer()
{
    stop();
}

void TonePlayer::setSequence(const QVector<Tone> &sequence)
{
    m_sequence = sequence;
}

void TonePlayer::stop()
{
    m_abort.storeRelaxed(1);
}

void TonePlayer::setDeviceName(const QString &deviceName)
{
    m_deviceName = deviceName;
}

void TonePlayer::setSampleRate(int sampleRate)
{
    m_sampleRate = sampleRate;
}

void TonePlayer::setVolume(double vol)
{
    m_volume = qBound(0.0, vol, 1.0);
}

void TonePlayer::playSequence(const QVector<Tone> &sequence)
{
    m_sequence = sequence;
    m_abort.storeRelaxed(0);
    play();
}

void TonePlayer::playCallInProgress()
{
    // "440:1500,0:3000,440:1500,0:3000,440:1500,0:0"
    QVector<Tone> seq;
    seq << Tone(440.0, 1500) << Tone(0.0, 3000)
        << Tone(440.0, 1500) << Tone(0.0, 3000)
        << Tone(440.0, 1500) << Tone(0.0, 0);
    playSequence(seq);
}

void TonePlayer::playUnreachable()
{
    // "950:330,1400:330,1800:330,0:0"
    QVector<Tone> seq;
    seq << Tone(950.0, 330) << Tone(1400.0, 330) << Tone(1800.0, 330)
        << Tone(0.0, 0);
    playSequence(seq);
}

void TonePlayer::playBusy()
{
    // "440:400,0:400,440:400,0:400,440:400,0:0"
    QVector<Tone> seq;
    seq << Tone(440.0, 400) << Tone(0.0, 400)
        << Tone(440.0, 400) << Tone(0.0, 400)
        << Tone(440.0, 400) << Tone(0.0, 0);
    playSequence(seq);
}

void TonePlayer::playClassicRingtone()
{
    // Unlike playCallInProgress/playUnreachable/playBusy, which are steady
    // single-frequency tones heard in the earpiece, this imitates the sound
    // of an electromechanical bell ringer: the clapper striking the gongs
    // produces a fast warble between two close pitches rather than one pure
    // tone. We approximate that by alternating two frequencies every 40 ms
    // (~12.5 Hz trill) for the classic 2 s on / 4 s off cadence, 3 bursts.
    const int trillSegment_ms = 40;
    const int ringDuration_ms = 2000;
    const int silenceDuration_ms = 4000;
    const double freqA = 1000.0;
    const double freqB = 1200.0;
    const int bursts = 3;

    QVector<Tone> seq;
    for (int burst = 0; burst < bursts; ++burst) {
        for (int t = 0; t < ringDuration_ms; t += trillSegment_ms) {
            int segLen = qMin(trillSegment_ms, ringDuration_ms - t);
            double freq = ((t / trillSegment_ms) % 2 == 0) ? freqA : freqB;
            seq << Tone(freq, segLen);
        }
        seq << Tone(0.0, burst == bursts - 1 ? 0 : silenceDuration_ms);
    }
    playSequence(seq);
}

static QString ff_errstr(int err)
{
    char buf[256] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromLocal8Bit(buf);
}

void TonePlayer::play()
{
    if (m_sequence.isEmpty()) {
        emit finishedPlaying();
        return;
    }

    avdevice_register_all();
#if LIBAVFORMAT_VERSION_MAJOR < 58
    av_register_all();
    avcodec_register_all();
#endif

    AVFormatContext *oc = nullptr;
    const QByteArray deviceUtf8 = m_deviceName.toUtf8();
    const char *device = deviceUtf8.constData();
    const QByteArray driverUtf8 = AudioDevice::PlatformDriverName().toUtf8();
    const char *driver = driverUtf8.constData();

    int ret = avformat_alloc_output_context2(&oc, nullptr, driver, device);
    if (ret < 0 || !oc) {
        QString msg = QString("Could not allocate '%1' output context: %2").arg(driver, ff_errstr(ret));
        qWarning() << msg;
        emit errorOccured(msg);
        return;
    }

    const AVCodecID codec_id = AV_CODEC_ID_PCM_S16LE;
    const AVCodec *codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        QString msg = "PCM encoder not found";
        qWarning() << msg;
        emit errorOccured(msg);
        avformat_free_context(oc);
        return;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        QString msg = "Could not alloc codec context";
        qWarning() << msg;
        emit errorOccured(msg);
        avformat_free_context(oc);
        return;
    }

    codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    codecCtx->sample_rate = m_sampleRate;
#if TP_HAVE_CH_LAYOUT
    av_channel_layout_default(&codecCtx->ch_layout, m_channels);
#else
    codecCtx->channels = m_channels;
    codecCtx->channel_layout = av_get_default_channel_layout(m_channels);
#endif
    codecCtx->time_base = AVRational{1, m_sampleRate};

    if ((ret = avcodec_open2(codecCtx, codec, nullptr)) < 0) {
        QString msg = QString("Could not open codec: %1").arg(ff_errstr(ret));
        qWarning() << msg;
        emit errorOccured(msg);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }

    AVStream *st = avformat_new_stream(oc, nullptr);
    if (!st) {
        QString msg = "Could not create stream";
        qWarning() << msg;
        emit errorOccured(msg);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }

    if ((ret = avcodec_parameters_from_context(st->codecpar, codecCtx)) < 0) {
        QString msg = QString("Failed to copy codec params: %1").arg(ff_errstr(ret));
        qWarning() << msg;
        emit errorOccured(msg);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }
    st->time_base = codecCtx->time_base;

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&oc->pb, device, AVIO_FLAG_WRITE)) < 0) {
            QString msg = QString("Could not open '%1' device '%2': %3").arg(driver, m_deviceName, ff_errstr(ret));
            qWarning() << msg;
            emit errorOccured(msg);
            avcodec_free_context(&codecCtx);
            avformat_free_context(oc);
            return;
        }
    }

    if ((ret = avformat_write_header(oc, nullptr)) < 0) {
        QString msg = QString("Error writing header: %1").arg(ff_errstr(ret));
        qWarning() << msg;
        emit errorOccured(msg);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        QString msg = "Could not allocate AVFrame";
        qWarning() << msg;
        emit errorOccured(msg);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }

    frame->format = codecCtx->sample_fmt;
    frame->sample_rate = codecCtx->sample_rate;
#if TP_HAVE_CH_LAYOUT
    if ((ret = av_channel_layout_copy(&frame->ch_layout, &codecCtx->ch_layout)) < 0) {
        QString msg = QString("Could not copy channel layout to frame: %1").arg(ff_errstr(ret));
        qWarning() << msg;
        emit errorOccured(msg);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }
#else
    frame->channels = codecCtx->channels;
    frame->channel_layout = codecCtx->channel_layout;
#endif

    int frame_size = codecCtx->frame_size;
    if (frame_size <= 0) frame_size = 1024;

    frame->nb_samples = frame_size;
    if ((ret = av_frame_get_buffer(frame, 0)) < 0) {
        QString msg = QString("Could not allocate initial frame buffer: %1 (frame_size=%2)")
                .arg(ff_errstr(ret)).arg(frame_size);
        qWarning() << msg;
        emit errorOccured(msg);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        avformat_free_context(oc);
        return;
    }

    int64_t pts = 0;
    double phase = 0.0;
    const double two_pi = 2.0 * M_PI;

#if TP_HAVE_CH_LAYOUT
    int chs = codecCtx->ch_layout.nb_channels;
#else
    int chs = codecCtx->channels;
#endif
    if (chs <= 0) chs = 1;

    for (int idx = 0; idx < m_sequence.size(); ++idx) {
        if (m_abort.loadRelaxed()) break;

        const Tone &t = m_sequence[idx];
        if (t.frequency == 0.0 && t.duration_ms == 0) {
            break;
        }

        if (t.duration_ms <= 0) continue;

        int64_t total_samples = (int64_t)qRound((m_sampleRate * t.duration_ms) / 1000.0);
        int64_t samples_written = 0;

        double phase_inc = 0.0;
        if (t.frequency > 0.0) {
            phase_inc = two_pi * t.frequency / double(m_sampleRate);
        }

        while (samples_written < total_samples) {
            if (m_abort.loadRelaxed()) break;

            int nb_samples = frame_size;
            if (nb_samples > (int)(total_samples - samples_written))
                nb_samples = int(total_samples - samples_written);

            if ((ret = av_frame_make_writable(frame)) < 0) {
                QString msg = QString("av_frame_make_writable failed: %1").arg(ff_errstr(ret));
                qWarning() << msg;
                emit errorOccured(msg);
                break;
            }
            frame->nb_samples = nb_samples;

            int16_t *out = reinterpret_cast<int16_t*>(frame->data[0]);

            for (int i = 0; i < nb_samples; ++i) {
                double sampleVal = 0.0;
                if (t.frequency > 0.0) {
                    sampleVal = sin(phase) * m_volume;
                    phase += phase_inc;
                    if (phase > 1e9 || phase < -1e9) phase = fmod(phase, two_pi);
                } else {
                    sampleVal = 0.0;
                }

                int16_t ival = static_cast<int16_t>(qBound(-1.0, sampleVal, 1.0) * 32767.0);
                for (int ch = 0; ch < chs; ++ch) {
                    out[i * chs + ch] = ival;
                }
            }

            frame->pts = pts;
            pts += frame->nb_samples;

            ret = avcodec_send_frame(codecCtx, frame);
            if (ret < 0) {
                QString msg = QString("Error sending frame to encoder: %1").arg(ff_errstr(ret));
                qWarning() << msg;
                emit errorOccured(msg);
                break;
            }

            while (ret >= 0) {
                AVPacket *pkt = av_packet_alloc();
                if (!pkt) {
                    QString msg = "av_packet_alloc failed";
                    qWarning() << msg;
                    emit errorOccured(msg);
                    ret = AVERROR(ENOMEM);
                    break;
                }

                ret = avcodec_receive_packet(codecCtx, pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_packet_free(&pkt);
                    break;
                } else if (ret < 0) {
                    QString msg = QString("Error encoding audio frame: %1").arg(ff_errstr(ret));
                    qWarning() << msg;
                    emit errorOccured(msg);
                    av_packet_free(&pkt);
                    break;
                }

                pkt->stream_index = st->index;

                ret = av_interleaved_write_frame(oc, pkt);
                if (ret < 0) {
                    QString msg = QString("Error writing packet to device: %1").arg(ff_errstr(ret));
                    qWarning() << msg;
                    emit errorOccured(msg);
                    av_packet_free(&pkt);
                    break;
                }

                av_packet_free(&pkt);
            }
            samples_written += nb_samples;
        }
        if (m_abort.loadRelaxed()) break;
    }

    avcodec_send_frame(codecCtx, nullptr);
    while (true) {
        AVPacket *pkt = av_packet_alloc();
        if (!pkt) break;
        ret = avcodec_receive_packet(codecCtx, pkt);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            av_packet_free(&pkt);
            break;
        } else if (ret < 0) {
            av_packet_free(&pkt);
            break;
        }
        pkt->stream_index = st->index;
        av_interleaved_write_frame(oc, pkt);
        av_packet_free(&pkt);
    }

    av_write_trailer(oc);

    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_free_context(oc);

    emit finishedPlaying();
}
