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
#include "capturegain.h"

#include <QDebug>
#include <QtGlobal>
#include <QStringList>
#include <cmath>

#ifdef Q_OS_LINUX
#include <alsa/asoundlib.h>
#endif

#ifdef HAVE_LIBPULSE
#include <pulse/pulseaudio.h>
#endif

// Per platform factories, defined next to the implementations themselves -
// same arrangement as NativeAudioBackend::create().
#ifdef Q_OS_WIN
CaptureGainControl *pica_create_wasapi_capture_gain(const QString &device);
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
CaptureGainControl *pica_create_coreaudio_capture_gain(const QString &device);
#endif

CaptureGainControl::~CaptureGainControl()
{
}

static double clampGain(double v)
{
	if (v < 0.0)
		return 0.0;
	if (v > 1.0)
		return 1.0;
	return v;
}

// PulseAudio is the one backend whose control goes past its nominal maximum -
// PA_VOLUME_NORM is 100% and the server will happily sit above it. Clamping a
// reading to 1.0 there would mean handing a user who had their source at 130%
// back at 100% when the call ended, so that path keeps whatever it read and
// only guards the bottom of the range and a runaway top.
static double clampGainAmplified(double v)
{
	if (v < 0.0)
		return 0.0;
	if (v > 2.5)
		return 2.5;
	return v;
}

#ifdef Q_OS_LINUX

// The mixer lives on a card, but what we are handed is a PCM name. The two
// namespaces overlap only by convention, so this covers the forms that
// actually turn up in the device list and falls back to the default card for
// anything else - which is right for plain "default" and no worse than
// nothing for the exotic cases.
static QString alsaCardFromPcm(const QString &pcm)
{
	int card = pcm.indexOf(QLatin1String("CARD="));
	if (card >= 0)
	{
		QString name = pcm.mid(card + 5);
		int comma = name.indexOf(QLatin1Char(','));
		if (comma >= 0)
			name = name.left(comma);
		if (!name.isEmpty())
			return QStringLiteral("hw:") + name;
	}

	// "hw:1,0", "plughw:CARD=PCH,DEV=0", "dsnoop:1" - everything before the
	// first comma after the colon names the card.
	int colon = pcm.indexOf(QLatin1Char(':'));
	if (colon >= 0)
	{
		QString rest = pcm.mid(colon + 1);
		int comma = rest.indexOf(QLatin1Char(','));
		if (comma >= 0)
			rest = rest.left(comma);
		if (!rest.isEmpty())
			return QStringLiteral("hw:") + rest;
	}

	return QStringLiteral("default");
}

class AlsaCaptureGain : public CaptureGainControl
{
public:
	AlsaCaptureGain() : m_mixer(nullptr), m_elem(nullptr), m_min(0), m_max(0) {}

	~AlsaCaptureGain() override
	{
		if (m_mixer)
			snd_mixer_close(m_mixer);
	}

	bool open(const QString &pcmName)
	{
		// The card the PCM name points at, and then the real cards as
		// fallbacks. "default" is not a card at all on a machine running a
		// sound server: attaching to it lands on the server's own software
		// volume, which is applied to samples the converter has already
		// digitised. That control cannot undo clipping - scaling a clipped
		// buffer down just gives a quieter clipped buffer - so calibrating
		// against it would turn the microphone down forever without the
		// clipping ever going away. Only a gain ahead of the ADC is any use
		// here, and tryCard() insists on one.
		QStringList candidates;
		candidates << alsaCardFromPcm(pcmName);

		if (candidates.first() == QLatin1String("default"))
		{
			for (int card = 0; card < 8; card++)
				candidates << (QStringLiteral("hw:") + QString::number(card));
		}

		for (int i = 0; i < candidates.size(); i++)
		{
			if (tryCard(candidates[i]))
				return true;
		}

		return false;
	}

	bool gain(double *out) override
	{
		if (!m_elem || !out)
			return false;

		// The kernel may have moved the control under us - another mixer app,
		// or the user's own volume keys.
		snd_mixer_handle_events(m_mixer);

		long v = 0;
		if (snd_mixer_selem_get_capture_volume(m_elem, SND_MIXER_SCHN_FRONT_LEFT, &v) < 0 &&
		    snd_mixer_selem_get_capture_volume(m_elem, SND_MIXER_SCHN_MONO, &v) < 0)
			return false;

		*out = clampGain(double(v - m_min) / double(m_max - m_min));
		return true;
	}

	bool gainDb(double *out) override
	{
		if (!m_elem || !out)
			return false;

		snd_mixer_handle_events(m_mixer);

		// ALSA reports hundredths of a decibel. Not every element has a dB
		// mapping - the software controls a sound server exposes often do not.
		long v = 0;
		if (snd_mixer_selem_get_capture_dB(m_elem, SND_MIXER_SCHN_FRONT_LEFT, &v) < 0 &&
		    snd_mixer_selem_get_capture_dB(m_elem, SND_MIXER_SCHN_MONO, &v) < 0)
			return false;

		*out = double(v) / 100.0;
		return true;
	}

	bool setGain(double value) override
	{
		if (!m_elem)
			return false;

		long v = m_min + (long)llround(clampGain(value) * double(m_max - m_min));
		return snd_mixer_selem_set_capture_volume_all(m_elem, v) >= 0;
	}

	QString description() const override { return m_name; }

private:
	// A candidate has to have a decibel range as well as a volume range. That
	// is what separates a converter gain from a sound server's software fader:
	// the hardware control knows what its steps are worth in dB, the software
	// one only has a slider position. It is also what lets the caller bound
	// the total cut in dB, which is the only unit that means the same thing on
	// every backend.
	bool tryCard(const QString &card)
	{
		if (m_mixer)
		{
			snd_mixer_close(m_mixer);
			m_mixer = nullptr;
			m_elem = nullptr;
		}

		if (snd_mixer_open(&m_mixer, 0) < 0)
		{
			m_mixer = nullptr;
			return false;
		}

		const QByteArray cardUtf8 = card.toUtf8();
		if (snd_mixer_attach(m_mixer, cardUtf8.constData()) < 0 ||
		    snd_mixer_selem_register(m_mixer, nullptr, nullptr) < 0 ||
		    snd_mixer_load(m_mixer) < 0)
			return false;

		// "Capture" is what the recording gain is called on essentially every
		// card that has one; "Mic" turns up on the ones that do not separate
		// the input selector from the gain.
		m_elem = findNamed("Capture");
		if (!m_elem)
			m_elem = findNamed("Mic");
		if (!m_elem)
			m_elem = findNamed("Digital");

		if (!m_elem)
			return false;

		if (snd_mixer_selem_get_capture_volume_range(m_elem, &m_min, &m_max) < 0 || m_max <= m_min)
			return false;

		long dbMin = 0, dbMax = 0;
		if (snd_mixer_selem_get_capture_dB_range(m_elem, &dbMin, &dbMax) < 0 || dbMax <= dbMin)
		{
			m_elem = nullptr;
			return false;
		}

		m_name = QString::fromUtf8(snd_mixer_selem_get_name(m_elem)) +
		         QStringLiteral(" on ") + card;
		return true;
	}

	snd_mixer_elem_t *findNamed(const char *want)
	{
		for (snd_mixer_elem_t *e = snd_mixer_first_elem(m_mixer); e; e = snd_mixer_elem_next(e))
		{
			if (!snd_mixer_selem_is_active(e) || !snd_mixer_selem_has_capture_volume(e))
				continue;
			if (qstrcmp(snd_mixer_selem_get_name(e), want) == 0)
				return e;
		}
		return nullptr;
	}

	snd_mixer_t *m_mixer;
	snd_mixer_elem_t *m_elem;
	long m_min;
	long m_max;
	QString m_name;
};

#endif // Q_OS_LINUX

#ifdef HAVE_LIBPULSE

// PulseAudio's introspection and its setters are both asynchronous, so this
// keeps a connected context with a private mainloop and pumps it until each
// operation has called back. Same shape as enumeratePulse() in
// audiodevice.cpp, but long lived, since a call makes several of these.
class PulseCaptureGain : public CaptureGainControl
{
public:
	PulseCaptureGain()
		: m_mainloop(nullptr), m_ctx(nullptr), m_channels(1),
		  m_pending(false), m_ok(false), m_value(0.0), m_valueDb(0.0) {}

	~PulseCaptureGain() override
	{
		if (m_ctx)
		{
			pa_context_disconnect(m_ctx);
			pa_context_unref(m_ctx);
		}
		if (m_mainloop)
			pa_mainloop_free(m_mainloop);
	}

	bool open(const QString &device)
	{
		m_mainloop = pa_mainloop_new();
		if (!m_mainloop)
			return false;

		m_ctx = pa_context_new(pa_mainloop_get_api(m_mainloop), "pica-client");
		if (!m_ctx)
			return false;

		if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
			return false;

		// Five seconds at 50ms a turn, so a server that never answers cannot
		// wedge the call setup.
		for (int i = 0; i < 100; i++)
		{
			pa_context_state_t st = pa_context_get_state(m_ctx);
			if (st == PA_CONTEXT_READY)
				break;
			if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)
				return false;
			if (!pump())
				return false;
		}

		if (pa_context_get_state(m_ctx) != PA_CONTEXT_READY)
			return false;

		m_source = device;
		if (m_source.isEmpty() || m_source == QLatin1String("default"))
		{
			if (!resolveDefaultSource())
				return false;
		}

		// Also establishes the channel count, which setGain() needs.
		double ignored;
		return gain(&ignored);
	}

	bool gain(double *out) override
	{
		if (!m_ctx || m_source.isEmpty())
			return false;

		const QByteArray name = m_source.toUtf8();
		m_pending = true;
		m_ok = false;

		pa_operation *op = pa_context_get_source_info_by_name(m_ctx, name.constData(),
		                                                      sourceInfoCb, this);
		if (!await(op))
			return false;

		if (m_ok && out)
			*out = m_value;
		return m_ok;
	}

	// pa_sw_volume_to_dB() returns -inf for a muted control, which is true but
	// useless as a number to do arithmetic on.
	bool gainDb(double *out) override
	{
		if (!out)
			return false;

		double ignored;
		if (!gain(&ignored))
			return false;

		if (m_valueDb < -200.0 || m_valueDb > 200.0)
			return false;

		*out = m_valueDb;
		return true;
	}

	bool setGain(double value) override
	{
		if (!m_ctx || m_source.isEmpty())
			return false;

		pa_cvolume cv;
		pa_cvolume_set(&cv, m_channels ? m_channels : 1,
		               (pa_volume_t)llround(clampGainAmplified(value) * double(PA_VOLUME_NORM)));

		const QByteArray name = m_source.toUtf8();
		m_pending = true;
		m_ok = false;

		pa_operation *op = pa_context_set_source_volume_by_name(m_ctx, name.constData(), &cv,
		                                                        successCb, this);
		if (!await(op))
			return false;

		return m_ok;
	}

	QString description() const override
	{
		return m_source + QStringLiteral(" (PulseAudio)");
	}

private:
	bool pump()
	{
		if (pa_mainloop_prepare(m_mainloop, 50 * 1000) < 0)
			return false;
		if (pa_mainloop_poll(m_mainloop) < 0)
			return false;
		if (pa_mainloop_dispatch(m_mainloop) < 0)
			return false;
		return true;
	}

	// Runs the loop until the callback for op has fired, then reaps it.
	bool await(pa_operation *op)
	{
		if (!op)
		{
			m_pending = false;
			return false;
		}

		for (int i = 0; i < 100 && m_pending; i++)
		{
			if (!pump())
				break;
		}

		bool finished = !m_pending;
		m_pending = false;
		pa_operation_unref(op);
		return finished;
	}

	bool resolveDefaultSource()
	{
		m_pending = true;
		m_ok = false;

		pa_operation *op = pa_context_get_server_info(m_ctx, serverInfoCb, this);
		if (!await(op))
			return false;

		return m_ok && !m_source.isEmpty();
	}

	static void serverInfoCb(pa_context *, const pa_server_info *info, void *userdata)
	{
		PulseCaptureGain *self = (PulseCaptureGain *)userdata;

		if (info && info->default_source_name)
		{
			self->m_source = QString::fromUtf8(info->default_source_name);
			self->m_ok = true;
		}
		self->m_pending = false;
	}

	static void sourceInfoCb(pa_context *, const pa_source_info *info, int eol, void *userdata)
	{
		PulseCaptureGain *self = (PulseCaptureGain *)userdata;

		if (eol)
		{
			self->m_pending = false;
			return;
		}

		if (!info)
			return;

		self->m_channels = info->volume.channels;
		// The loudest channel, so a step down is a step down for all of them
		// rather than only for whichever happens to be quietest.
		self->m_value = clampGainAmplified(double(pa_cvolume_max(&info->volume)) / double(PA_VOLUME_NORM));
		self->m_valueDb = pa_sw_volume_to_dB(pa_cvolume_max(&info->volume));
		self->m_ok = true;
	}

	static void successCb(pa_context *, int success, void *userdata)
	{
		PulseCaptureGain *self = (PulseCaptureGain *)userdata;
		self->m_ok = (success != 0);
		self->m_pending = false;
	}

	pa_mainloop *m_mainloop;
	pa_context *m_ctx;
	QString m_source;
	uint8_t m_channels;

	bool m_pending;
	bool m_ok;
	double m_value;
	double m_valueDb;
};

#endif // HAVE_LIBPULSE

CaptureGainControl *CaptureGainControl::create(const QString &driver, const QString &device)
{
#ifdef Q_OS_WIN
	if (driver == QLatin1String("platform-wasapi"))
		return pica_create_wasapi_capture_gain(device);
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	if (driver == QLatin1String("platform-coreaudio"))
		return pica_create_coreaudio_capture_gain(device);
#endif

#ifdef HAVE_LIBPULSE
	if (driver == QLatin1String("pulse"))
	{
		PulseCaptureGain *g = new PulseCaptureGain();
		if (g->open(device))
			return g;
		delete g;
		qWarning() << "No PulseAudio volume control for capture source" << device;
		return nullptr;
	}
#endif

#ifdef Q_OS_LINUX
	if (driver == QLatin1String("alsa"))
	{
		AlsaCaptureGain *g = new AlsaCaptureGain();
		if (g->open(device))
			return g;
		delete g;
		qWarning() << "No ALSA capture volume control for device" << device;
		return nullptr;
	}
#endif

	Q_UNUSED(driver)
	Q_UNUSED(device)
	return nullptr;
}
