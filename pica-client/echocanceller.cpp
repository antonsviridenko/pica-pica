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
#include "echocanceller.h"
#include "audiodevice.h"
#include "settings.h"
#include "globals.h"

#include <QDebug>
#include <QMutexLocker>
#include <cmath>
#include <cstring>

// Which major version of webrtc-audio-processing this is being built against.
// 1.x and 2.x are separate parallel-installable libraries with separate
// pkg-config names, and 2.x broke the API in a few places - the conditionals
// on this below are the whole of what differs as far as this file is
// concerned.
//
// configure defines it, and so does the qmake build. The fallback is for
// anything that builds this file without either: api/audio/audio_processing.h
// is where 2.x moved the header that 1.x only had under modules/, so its
// presence is the version.
#ifndef PICA_WEBRTC_AUDIO_PROCESSING_MAJOR
#  if defined(__has_include) && __has_include(<api/audio/audio_processing.h>)
#    define PICA_WEBRTC_AUDIO_PROCESSING_MAJOR 2
#  else
#    define PICA_WEBRTC_AUDIO_PROCESSING_MAJOR 1
#  endif
#endif

// Still the right include on 2.x, where it is a stub forwarding to
// api/audio/audio_processing.h - and it is installed there, so there is
// nothing to switch on here.
#include <modules/audio_processing/include/audio_processing.h>

// Must come after the above, and not only as a matter of taste: on 2.x this
// header declares parameters as absl::Nullable/absl::Nonnull without
// including the abseil header that defines them, so on its own it does not
// compile. audio_processing.h is what pulls that in. Both versions include
// this themselves anyway; it is named here so that the scoped_refptr below is
// not silently relying on somebody else's include.
#include <api/scoped_refptr.h>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

// Same split as audiodevice.cpp - FFmpeg 5.1 replaced the channel count and
// layout fields with the AVChannelLayout API.
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#  define EC_HAVE_CH_LAYOUT 1
#else
#  define EC_HAVE_CH_LAYOUT 0
#endif

// Setting values. Words rather than numbers because this is a three way
// choice that will be read by whoever is looking at a user's config, and a
// stray "2" says nothing.
static const char kEchoCancellationOwn[] = "own";
static const char kEchoCancellationPlatform[] = "platform";
static const char kEchoCancellationNone[] = "off";

EchoCancellationMode loadEchoCancellationSetting()
{
	Settings st(config_dbname);

	const QString value = st.loadValue("audio.echo_cancellation", QString()).toString();

	if (value == QLatin1String(kEchoCancellationOwn))
		return EchoCancellationOwn;

	if (value == QLatin1String(kEchoCancellationPlatform))
	{
		// The stored choice can stop being available underneath the user -
		// the audio system moved from PulseAudio to ALSA, say. Leaving it as
		// it stands would mean nobody cancelling and nothing saying so.
		if (AudioDevice::PlatformEchoCancellationAvailable())
			return EchoCancellationPlatform;

		qWarning() << "Echo cancellation is set to the platform's own, but the audio system in "
		              "use does not provide one - falling back to ours";
		return EchoCancellationOwn;
	}

	if (value == QLatin1String(kEchoCancellationNone))
		return EchoCancellationNone;

	// Nothing stored under the current key. An install that predates it may
	// still have the on/off setting this replaced, and someone who turned
	// cancellation off did mean it.
	if (!st.loadValue("audio.echo_cancel", 1).toBool())
		return EchoCancellationNone;

	// The platform's own is the better one where it exists, so it is the
	// default there; elsewhere the default is ours, which is what the plain
	// on/off setting used to mean.
	return AudioDevice::PlatformEchoCancellationAvailable()
	       ? EchoCancellationPlatform
	       : EchoCancellationOwn;
}

void storeEchoCancellationSetting(EchoCancellationMode mode)
{
	Settings st(config_dbname);

	switch (mode)
	{
	case EchoCancellationPlatform:
		st.storeValue("audio.echo_cancellation", QLatin1String(kEchoCancellationPlatform));
		break;

	case EchoCancellationNone:
		st.storeValue("audio.echo_cancellation", QLatin1String(kEchoCancellationNone));
		break;

	case EchoCancellationOwn:
	default:
		st.storeValue("audio.echo_cancellation", QLatin1String(kEchoCancellationOwn));
		break;
	}
}

// Holds everything that needs the WebRTC headers, so that echocanceller.h -
// which audiodevice.h includes, and so half the client with it - does not.
struct EchoCancellerPrivate
{
	rtc::scoped_refptr<webrtc::AudioProcessing> apm;

	// Both directions are mono at our rate. The integer interface requires
	// the capture, playback and render rates to be the same, which is why
	// pushFarEnd() resamples rather than passing the peer's rate through.
	webrtc::StreamConfig stream;
};

bool EchoCanceller::isNativeRate(int rate)
{
	return rate == 8000 || rate == 16000 || rate == 32000 || rate == 48000;
}

EchoCanceller::EchoCanceller(int sampleRate)
	: m_sampleRate(sampleRate),
	  m_frameSize(0),
	  m_d(nullptr),
	  m_farSwr(nullptr),
	  m_farSwrRate(0),
	  m_nearEnergy(0.0),
	  m_farEnergy(0.0),
	  m_processedSamples(0),
	  m_nextReportSamples(0),
	  m_renderBlocks(0),
	  m_captureBlocks(0)
{
	if (!isNativeRate(m_sampleRate))
	{
		qWarning() << "Echo canceller:" << m_sampleRate
		           << "Hz is not one of AEC3's rates (8/16/32/48 kHz) - not cancelling";
		return;
	}

	// AEC3 works on 10ms of audio at a time, everywhere, at every rate.
	m_frameSize = m_sampleRate / 100;

	webrtc::AudioProcessing::Config cfg;

	cfg.echo_canceller.enabled = true;
	// AEC3 proper. The mobile mode is the cut down fixed point canceller for
	// phones, and is worse anywhere that can afford not to use it.
	cfg.echo_canceller.mobile_mode = false;
	cfg.echo_canceller.enforce_high_pass_filtering = true;

	cfg.high_pass_filter.enabled = true;

	// Costs little next to the filter, and a lower noise floor is what lets
	// the residual echo suppressor tell echo from room noise.
	cfg.noise_suppression.enabled = true;
	cfg.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kModerate;

	// Both AGCs stay off. The call has no level control anywhere else, and a
	// gain riding the microphone moves the echo path, which is the one thing
	// an adaptive filter cannot follow.
	cfg.gain_controller1.enabled = false;
	cfg.gain_controller2.enabled = false;

	cfg.transient_suppression.enabled = false;

#if PICA_WEBRTC_AUDIO_PROCESSING_MAJOR < 2
	// All three were dropped in 2.x. The first two only gate statistics we
	// never read; the third is a second, statistical echo estimator whose
	// output we do not use either. Switching them off on 1.x is therefore
	// the same configuration 2.x gives us for nothing.
	cfg.voice_detection.enabled = false;
	cfg.level_estimation.enabled = false;
	cfg.residual_echo_detector.enabled = false;
#endif

	m_d = new EchoCancellerPrivate;
	m_d->stream = webrtc::StreamConfig(m_sampleRate, 1);

#if PICA_WEBRTC_AUDIO_PROCESSING_MAJOR >= 2
	m_d->apm = webrtc::AudioProcessingBuilder().Create();
#else
	// 1.x hands back a raw pointer to an object whose reference count is
	// still zero - the scoped_refptr is what takes it to one, and dropping
	// the result on the floor would leak rather than free.
	m_d->apm = rtc::scoped_refptr<webrtc::AudioProcessing>(webrtc::AudioProcessingBuilder().Create());
#endif

	if (!m_d->apm)
	{
		qWarning() << "Could not create the WebRTC audio processing module";
		delete m_d;
		m_d = nullptr;
		return;
	}

	m_d->apm->ApplyConfig(cfg);

	// Mono at our rate in both directions, which the integer interface
	// requires anyway. Spelled out through a ProcessingConfig rather than the
	// shorter Initialize() overload taking channel layouts, because that
	// overload and the ChannelLayout enum it needs are gone in 2.x.
	webrtc::ProcessingConfig streams;
	streams.input_stream() = m_d->stream;
	streams.output_stream() = m_d->stream;
	streams.reverse_input_stream() = m_d->stream;
	streams.reverse_output_stream() = m_d->stream;

	const int err = m_d->apm->Initialize(streams);
	if (err != webrtc::AudioProcessing::kNoError)
	{
		qWarning() << "Could not initialise the WebRTC audio processing module:" << err;
		delete m_d;
		m_d = nullptr;
		return;
	}

	// Hold off the first report until there is a window's worth of audio
	// behind the averages, otherwise it only says the call just started.
	m_nextReportSamples = qint64(m_sampleRate) * kReportIntervalSec;

	// The library version is in there because it is otherwise invisible which
	// of the two parallel-installable ones got linked, and they behave
	// differently enough to be worth knowing about from a log alone.
	qDebug("Echo canceller: WebRTC AEC3 (webrtc-audio-processing-%d), %d Hz, frame %d samples",
	       PICA_WEBRTC_AUDIO_PROCESSING_MAJOR, m_sampleRate, m_frameSize);
}

EchoCanceller::~EchoCanceller()
{
	delete m_d;

	if (m_farSwr)
		swr_free(&m_farSwr);
}

bool EchoCanceller::isValid() const
{
	return m_d != nullptr;
}

void EchoCanceller::pushFarEnd(const qint16 *pcm, int nsamples, int srcRate)
{
	if (!m_d || !pcm || nsamples <= 0)
		return;

	QMutexLocker locker(&m_renderMutex);

	if (srcRate > 0 && srcRate != m_sampleRate)
	{
		if (m_farSwr && m_farSwrRate != srcRate)
		{
			swr_free(&m_farSwr);
			m_farSwrRate = 0;
		}

		if (!m_farSwr)
		{
#if EC_HAVE_CH_LAYOUT
			AVChannelLayout mono;
			av_channel_layout_default(&mono, 1);
			int ret = swr_alloc_set_opts2(&m_farSwr, &mono, AV_SAMPLE_FMT_S16, m_sampleRate,
			                              &mono, AV_SAMPLE_FMT_S16, srcRate, 0, nullptr);
			av_channel_layout_uninit(&mono);
			if (ret < 0)
				m_farSwr = nullptr;
#else
			m_farSwr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, m_sampleRate,
			                              AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, srcRate, 0, nullptr);
#endif
			if (m_farSwr && swr_init(m_farSwr) < 0)
				swr_free(&m_farSwr);

			if (!m_farSwr)
			{
				qWarning() << "Echo canceller: cannot resample" << srcRate << "Hz far end to"
				           << m_sampleRate << "Hz - echo cancellation will not work";
				return;
			}

			m_farSwrRate = srcRate;
			qDebug() << "Echo canceller: far end is" << srcRate << "Hz, resampling to" << m_sampleRate << "Hz";
		}

		// Upper bound on what this many input samples can become, plus
		// whatever the converter is still holding from last time.
		int room = (int)av_rescale_rnd(swr_get_delay(m_farSwr, srcRate) + nsamples,
		                              m_sampleRate, srcRate, AV_ROUND_UP);
		if (room <= 0)
			return;

		m_farScratch.resize(room);

		uint8_t *out[1] = { (uint8_t *)m_farScratch.data() };
		const uint8_t *in[1] = { (const uint8_t *)pcm };

		int got = swr_convert(m_farSwr, out, room, in, nsamples);
		if (got <= 0)
			return;

		pcm = m_farScratch.constData();
		nsamples = got;
	}

	int base = m_farPending.size();
	m_farPending.resize(base + nsamples);
	memcpy(m_farPending.data() + base, pcm, nsamples * sizeof(qint16));

	double farEnergy = 0.0;
	int blocks = m_farPending.size() / m_frameSize;

	for (int b = 0; b < blocks; b++)
	{
		qint16 *farBlock = m_farPending.data() + b * m_frameSize;

		for (int i = 0; i < m_frameSize; i++)
			farEnergy += double(farBlock[i]) * double(farBlock[i]);

		// In place: the module is happy for source and destination to be the
		// same buffer, and nothing downstream wants the processed render
		// signal - handing it over is only how AEC3 learns what is about to
		// come back through the microphone.
		m_d->apm->ProcessReverseStream(farBlock, m_d->stream, m_d->stream, farBlock);
	}

	if (blocks > 0)
	{
		m_farPending.remove(0, blocks * m_frameSize);

		QMutexLocker statLocker(&m_statMutex);
		m_farEnergy = m_farEnergy * 0.95 + (farEnergy / (blocks * m_frameSize)) * 0.05;
		m_renderBlocks += blocks;
	}
}

void EchoCanceller::processNearEnd(qint16 *pcm, int nsamples)
{
	if (!m_d || !pcm || nsamples < m_frameSize)
		return;

	double nearEnergy = 0.0;
	int blocks = nsamples / m_frameSize;

	{
		QMutexLocker locker(&m_captureMutex);

		for (int b = 0; b < blocks; b++)
		{
			// Not called "near" - windef.h still defines that as an empty
			// macro on the mingw-w64 builds.
			qint16 *nearBlock = pcm + b * m_frameSize;

			for (int i = 0; i < m_frameSize; i++)
				nearEnergy += double(nearBlock[i]) * double(nearBlock[i]);

			m_d->apm->ProcessStream(nearBlock, m_d->stream, m_d->stream, nearBlock);
		}
	}

	// At least one whole block, guaranteed by the nsamples check on entry.
	const int used = blocks * m_frameSize;
	bool report = false;

	{
		QMutexLocker statLocker(&m_statMutex);

		// Exponential forgetting, so the levels reflect the last few seconds
		// rather than the whole call. Per sample, so a change in the capture
		// path's block size does not move them.
		m_nearEnergy = m_nearEnergy * 0.95 + (nearEnergy / used) * 0.05;

		m_processedSamples += used;
		m_captureBlocks += blocks;
		if (m_processedSamples >= m_nextReportSamples)
		{
			m_nextReportSamples = m_processedSamples + qint64(m_sampleRate) * kReportIntervalSec;
			report = true;
		}
	}

	// Outside the lock: the accessors below take it themselves, and qDebug()
	// does I/O that has no business happening with a mutex held on the
	// capture thread. The values can shift by one block in between, which
	// does not matter for a log line.
	if (report)
	{
		int delayMs = 0;
		double erleDb = 0.0;
		double erlDb = 0.0;
		bool haveErl = false;

		{
			QMutexLocker locker(&m_captureMutex);
			webrtc::AudioProcessingStats stats = m_d->apm->GetStatistics();

			if (stats.delay_ms.has_value())
				delayMs = *stats.delay_ms;
			if (stats.echo_return_loss_enhancement.has_value())
				erleDb = *stats.echo_return_loss_enhancement;
			if (stats.echo_return_loss.has_value())
			{
				erlDb = *stats.echo_return_loss;
				haveErl = true;
			}
		}

		qint64 lead = 0;
		{
			QMutexLocker statLocker(&m_statMutex);
			lead = m_renderBlocks - m_captureBlocks;
		}

		// erl says how strongly the speaker couples back into the microphone,
		// which is whether there is anything worth cancelling at all. AEC3
		// reports it negated (echo_remover.cc: -10*log10(erl)), so the more
		// negative it is the weaker the coupling - a headset reads well down,
		// speakers in a small room read close to zero.
		//
		// erle is how much of that coupling the filter is actually removing.
		// It sits at its 0 dB floor (Erle::min = 1.f) until the filter
		// converges, so a call that never leaves +0.2 dB is a call where the
		// linear filter never got anywhere.
		//
		// The delay is what AEC3 has aligned the two streams at. Zero is not
		// a delay: block_processor.cc reports 0 when the estimator has no
		// value, so it means the filter is running unaligned and the erle
		// beside it cannot be anything but the floor.
		QString delayText = delayMs > 0
		                    ? QString("echo delay %1 ms").arg(delayMs)
		                    : QStringLiteral("NOT ALIGNED");

		QString erlText = haveErl
		                  ? QString("erl %1 dB").arg(erlDb, 0, 'f', 1)
		                  : QStringLiteral("erl n/a");

		qDebug("AEC: erle %+.1f dB  %s  mic %.1f dBFS  far %.1f dBFS  %s  render lead %d ms",
		       erleDb, qPrintable(erlText), nearEndLevelDb(), farEndLevelDb(),
		       qPrintable(delayText), (int)(lead * 10));   // a block is 10ms, always
	}
}

double EchoCanceller::erle()
{
	if (!m_d)
		return 0.0;

	QMutexLocker locker(&m_captureMutex);

	webrtc::AudioProcessingStats stats = m_d->apm->GetStatistics();

	return stats.echo_return_loss_enhancement.has_value()
	       ? *stats.echo_return_loss_enhancement
	       : 0.0;
}

// Mean square of 16 bit samples to dBFS, with a floor so digital silence
// prints as a number rather than -inf.
static double level_db(double meanSquare)
{
	static const double kFullScale = 32768.0 * 32768.0;

	if (meanSquare <= 0.0)
		return -120.0;

	double db = 10.0 * log10(meanSquare / kFullScale);
	return db < -120.0 ? -120.0 : db;
}

double EchoCanceller::nearEndLevelDb()
{
	QMutexLocker locker(&m_statMutex);
	return level_db(m_nearEnergy);
}

double EchoCanceller::farEndLevelDb()
{
	QMutexLocker locker(&m_statMutex);
	return level_db(m_farEnergy);
}
