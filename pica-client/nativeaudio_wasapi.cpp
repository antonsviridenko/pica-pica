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
#include "nativeaudio.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QObject>
#include <QVector>
#include <cstring>

// INITGUID has to come first so the CLSIDs and IIDs used below get defined
// here rather than expecting a separate GUID library at link time.
#define INITGUID
#include <initguid.h>

#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#  define NA_HAVE_CH_LAYOUT 1
#else
#  define NA_HAVE_CH_LAYOUT 0
#endif

// How much of a buffer to ask WASAPI for. Shared mode rounds this up to the
// engine period anyway; 40 ms is two of our 20 ms call packets, enough that a
// scheduling hiccup does not turn into a glitch without adding audible delay.
static const REFERENCE_TIME kBufferDuration = 400000; // in 100ns units

static QString hrString(HRESULT hr)
{
	return QString("0x%1").arg((quint32)hr, 8, 16, QLatin1Char('0'));
}

static QString wideToQString(LPCWSTR s)
{
	return s ? QString::fromWCharArray(s) : QString();
}

// Recovers the WAVE_FORMAT_* tag from a WAVEFORMATEXTENSIBLE subformat GUID.
//
// The subformats a shared mode mix format uses are the generic ones derived
// from a wave format tag: the tag sits in Data1 and everything after it is a
// fixed suffix. Reading it out that way rather than comparing against
// KSDATAFORMAT_SUBTYPE_PCM and KSDATAFORMAT_SUBTYPE_IEEE_FLOAT is deliberate -
// mingw-w64 declares those two but does not define them, the definitions
// being in ksuser, and INITGUID does not help because they are spelled with
// DEFINE_GUIDSTRUCT rather than DEFINE_GUID. Not worth a link dependency for
// two constants whose value is this predictable.
//
// Returns false for a subformat that is not one of the generic family, which
// is a codec (AC-3 and such) rather than something we could hand PCM to.
static bool ksSubFormatTag(const GUID *g, WORD *tag)
{
	static const unsigned char suffix[8] =
		{ 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };

	if (g->Data2 != 0x0000 || g->Data3 != 0x0010)
		return false;

	if (memcmp(g->Data4, suffix, sizeof(suffix)) != 0)
		return false;

	if (g->Data1 > 0xFFFF)
		return false;

	*tag = (WORD)g->Data1;
	return true;
}

// Works out which AVSampleFormat a WASAPI mix format corresponds to.
// Shared mode engines are float32 in practice, but integer formats turn up on
// some drivers and cost nothing to support. Returns AV_SAMPLE_FMT_NONE for
// anything we cannot describe, which sends the caller back to reporting a
// plain unsupported-format failure.
static AVSampleFormat waveFormatToAv(const WAVEFORMATEX *wfx)
{
	WORD tag = wfx->wFormatTag;

	if (tag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22)
	{
		const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)wfx;
		if (!ksSubFormatTag(&ext->SubFormat, &tag))
			return AV_SAMPLE_FMT_NONE;
	}

	if (tag == WAVE_FORMAT_IEEE_FLOAT)
		return wfx->wBitsPerSample == 64 ? AV_SAMPLE_FMT_DBL : AV_SAMPLE_FMT_FLT;

	if (tag == WAVE_FORMAT_PCM)
	{
		switch (wfx->wBitsPerSample)
		{
		case 16: return AV_SAMPLE_FMT_S16;
		case 32: return AV_SAMPLE_FMT_S32;
		default: return AV_SAMPLE_FMT_NONE;
		}
	}

	return AV_SAMPLE_FMT_NONE;
}

static SwrContext *makeResampler(AVSampleFormat inFmt, int inRate, int inCh,
                                 AVSampleFormat outFmt, int outRate, int outCh)
{
	SwrContext *swr = nullptr;

#if NA_HAVE_CH_LAYOUT
	AVChannelLayout inLayout, outLayout;
	av_channel_layout_default(&inLayout, inCh);
	av_channel_layout_default(&outLayout, outCh);
	int ret = swr_alloc_set_opts2(&swr, &outLayout, outFmt, outRate,
	                              &inLayout, inFmt, inRate, 0, nullptr);
	av_channel_layout_uninit(&inLayout);
	av_channel_layout_uninit(&outLayout);
	if (ret < 0)
		return nullptr;
#else
	swr = swr_alloc_set_opts(nullptr,
	                         av_get_default_channel_layout(outCh), outFmt, outRate,
	                         av_get_default_channel_layout(inCh), inFmt, inRate,
	                         0, nullptr);
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

class WasapiBackend : public NativeAudioBackend
{
public:
	WasapiBackend()
		: m_comInitialized(false), m_client(nullptr), m_capture(nullptr),
		  m_render(nullptr), m_event(nullptr), m_mixFormat(nullptr),
		  m_isCapture(false), m_sampleRate(48000), m_channels(1),
		  m_devSampleRate(48000), m_devChannels(1), m_devFormat(AV_SAMPLE_FMT_S16),
		  m_devBytesPerFrame(2), m_swr(nullptr), m_bufferFrames(0),
		  m_platformAec(false), m_started(false), m_spillOffset(0)
	{
	}

	~WasapiBackend() override
	{
		close();
	}

	QString apiName() const override { return QStringLiteral("wasapi"); }

	bool openCapture(const QString &device, int sampleRate, int channels,
	                 NativeAudioMode mode, QString *error) override
	{
		return open(device, sampleRate, channels, true, mode, error);
	}

	bool openPlayback(const QString &device, int sampleRate, int channels,
	                  NativeAudioMode mode, QString *error) override
	{
		return open(device, sampleRate, channels, false, mode, error);
	}

	int read(qint16 *buf, int maxSamples, int timeoutMs) override;
	int write(const qint16 *buf, int nsamples, int timeoutMs) override;
	void close() override;

	bool hasPlatformEchoCancellation() const override { return m_platformAec; }

	QString formatDescription() const override
	{
		if (!m_client)
			return QString();

		return QString("WASAPI %1 %2 Hz %3 ch %4 -> %5 Hz %6 ch s16, buffer %7 fr%8")
		       .arg(m_isCapture ? "in" : "out")
		       .arg(m_devSampleRate).arg(m_devChannels)
		       .arg(QLatin1String(av_get_sample_fmt_name(m_devFormat)))
		       .arg(m_sampleRate).arg(m_channels)
		       .arg(m_bufferFrames)
		       .arg(m_platformAec ? ", platform AEC" : "");
	}

private:
	bool open(const QString &device, int sampleRate, int channels, bool isCapture,
	          NativeAudioMode mode, QString *error);
	bool initCom();
	IMMDevice *resolveDevice(const QString &device, bool isCapture, NativeAudioMode mode, QString *error);
	void requestCommunicationsCategory();
	int appendConverted(const BYTE *deviceData, int deviceFrames, bool silent,
	                    qint16 *buf, int maxSamples, int written);

	bool m_comInitialized;
	IAudioClient *m_client;
	IAudioCaptureClient *m_capture;
	IAudioRenderClient *m_render;
	HANDLE m_event;
	WAVEFORMATEX *m_mixFormat;

	bool m_isCapture;

	// What our own pipeline speaks: interleaved 16 bit, m_channels wide.
	int m_sampleRate;
	int m_channels;

	// What the stream was actually opened with. Equal to the above whenever
	// WASAPI accepted our format directly, which is the normal case; when it
	// did not, m_swr bridges the two.
	int m_devSampleRate;
	int m_devChannels;
	AVSampleFormat m_devFormat;
	int m_devBytesPerFrame;
	SwrContext *m_swr;

	UINT32 m_bufferFrames;
	bool m_platformAec;
	bool m_started;

	// WASAPI hands capture audio over one packet at a time and will not let a
	// packet be released partially, so whatever the caller did not have room
	// for is kept here until the next read().
	QVector<qint16> m_spill;
	int m_spillOffset;

	// Scratch space for format conversion, kept between calls so the audio
	// loop does not allocate.
	QVector<qint16> m_convIn;
	QVector<quint8> m_convOut;
};

bool WasapiBackend::initCom()
{
	// Every thread that touches COM has to initialise it, and Capture() and
	// Play() each run on their own QThread. MULTITHREADED is what the WASAPI
	// samples use and what suits a worker thread with no message loop.
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (hr == RPC_E_CHANGED_MODE)
	{
		// Somebody else already put this thread in a single threaded
		// apartment. That is still usable, but it is not ours to undo.
		return true;
	}
	if (FAILED(hr))
		return false;

	m_comInitialized = true;
	return true;
}

IMMDevice *WasapiBackend::resolveDevice(const QString &device, bool isCapture,
                                        NativeAudioMode mode, QString *error)
{
	IMMDeviceEnumerator *devEnum = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
	                              IID_IMMDeviceEnumerator, (void **)&devEnum);
	if (FAILED(hr) || !devEnum)
	{
		if (error)
			*error = QString("Could not create the audio endpoint enumerator: %1").arg(hrString(hr));
		return nullptr;
	}

	IMMDevice *dev = nullptr;
	if (device.isEmpty() || device == QLatin1String("default"))
	{
		// eCommunications rather than eConsole for a call: this is the
		// endpoint the user nominated for calls, and asking for it by that
		// role is part of what tells Windows to treat the stream as voice.
		// A ringtone is not a call and belongs on the console endpoint, so
		// that it is heard on the speakers rather than in a headset the user
		// is not wearing yet.
		const ERole role = (mode == NativeAudioVoiceCall) ? eCommunications : eConsole;
		hr = devEnum->GetDefaultAudioEndpoint(isCapture ? eCapture : eRender, role, &dev);
	}
	else
	{
		hr = devEnum->GetDevice((LPCWSTR)device.utf16(), &dev);
	}

	devEnum->Release();

	if (FAILED(hr) || !dev)
	{
		if (error)
			*error = QString("Could not open audio endpoint '%1': %2").arg(device, hrString(hr));
		return nullptr;
	}

	return dev;
}

void WasapiBackend::requestCommunicationsCategory()
{
#ifdef __IAudioClient2_INTERFACE_DEFINED__
	// This is the part that buys us the platform's echo cancellation. Marking
	// the stream AudioCategory_Communications before Initialize() puts it
	// through the communications signal processing chain, which is where the
	// AEC, noise suppression and gain control that Windows (or the endpoint's
	// own APO) provides live. Without it we get the raw capture stream and
	// have to cancel the echo ourselves.
	//
	// Whether an AEC is actually in that chain depends on the driver, so this
	// is a request, not a guarantee - see hasPlatformEchoCancellation().
	IAudioClient2 *client2 = nullptr;
	if (SUCCEEDED(m_client->QueryInterface(IID_IAudioClient2, (void **)&client2)) && client2)
	{
		AudioClientProperties props;
		// ZeroMemory also covers Options, which only exists from the Windows
		// 8.1 SDK on and whose "none" value is zero in every version that has
		// it - so this compiles against either header.
		ZeroMemory(&props, sizeof(props));
		props.cbSize = sizeof(props);
		props.bIsOffload = FALSE;
		props.eCategory = AudioCategory_Communications;

		HRESULT phr = client2->SetClientProperties(&props);
		if (SUCCEEDED(phr))
			m_platformAec = true;
		else
			qWarning() << "WASAPI: SetClientProperties failed" << hrString(phr)
			           << "- no platform echo cancellation on this stream";

		client2->Release();
	}
	else
	{
		qWarning() << "WASAPI: IAudioClient2 not available - no platform echo cancellation on this stream";
	}
#else
	qWarning() << "WASAPI: built without IAudioClient2 - no platform echo cancellation on this stream";
#endif
}

bool WasapiBackend::open(const QString &device, int sampleRate, int channels, bool isCapture,
                         NativeAudioMode mode, QString *error)
{
	close();

	if (!initCom())
	{
		if (error)
			*error = QStringLiteral("Could not initialise COM on the audio thread");
		return false;
	}

	m_isCapture = isCapture;
	m_sampleRate = sampleRate;
	m_channels = channels;

	IMMDevice *dev = resolveDevice(device, isCapture, mode, error);
	if (!dev)
	{
		close();
		return false;
	}

	HRESULT hr = dev->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void **)&m_client);
	dev->Release();
	if (FAILED(hr) || !m_client)
	{
		if (error)
			*error = QString("Could not activate the audio client: %1").arg(hrString(hr));
		close();
		return false;
	}

	// Capture only, deliberately.
	//
	// On Windows the echo canceller lives in the capture endpoint's
	// processing chain and takes its reference from the render endpoint
	// itself, not from any particular render stream - so a render stream
	// gains nothing from the communications category, while still paying for
	// whatever the endpoint's communications APO does to it. Which endpoint a
	// call plays to is a separate question, settled by the eCommunications
	// role in resolveDevice().
	//
	// Asking for it on render as well is what made the settings dialog's
	// microphone test come out chopped while the ring test, which opens a
	// plain stream, was clean.
	if (mode == NativeAudioVoiceCall && isCapture)
		requestCommunicationsCategory();

	// Always run on the engine's own mix format and convert on this side,
	// rather than asking WASAPI to hand us mono 16 bit through
	// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM.
	//
	// The flag is documented to insert a channel matrixer and a rate
	// converter, and Initialize() reports success when it is set - but there
	// is no way afterwards to ask what format the stream really ended up in,
	// so a driver that quietly ignores it leaves us reading a stereo buffer
	// as if it were mono. That takes half the samples of every packet,
	// discards the rest with ReleaseBuffer(), and renders interleaved L,R
	// pairs as consecutive mono samples: audio that is both chopped and
	// running at half speed, which is exactly what the microphone test did.
	//
	// The mix format is the one format shared mode is guaranteed to accept,
	// GetMixFormat() tells us exactly what it is, and libswresample is
	// already linked and already doing this work on every other platform.
	// One code path, no trust required.
	hr = m_client->GetMixFormat(&m_mixFormat);
	if (FAILED(hr) || !m_mixFormat)
	{
		if (error)
			*error = QString("Could not query the audio engine mix format: %1").arg(hrString(hr));
		close();
		return false;
	}

	m_devFormat = waveFormatToAv(m_mixFormat);
	if (m_devFormat == AV_SAMPLE_FMT_NONE)
	{
		if (error)
			*error = QString("The audio engine mix format is not one we can convert (tag %1, %2 bits)")
			         .arg(m_mixFormat->wFormatTag).arg(m_mixFormat->wBitsPerSample);
		close();
		return false;
	}

	m_devSampleRate = (int)m_mixFormat->nSamplesPerSec;
	m_devChannels = (int)m_mixFormat->nChannels;
	m_devBytesPerFrame = (int)m_mixFormat->nBlockAlign;

	hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
	                          kBufferDuration, 0, m_mixFormat, nullptr);
	if (FAILED(hr))
	{
		if (error)
			*error = QString("Could not initialise the audio client: %1").arg(hrString(hr));
		close();
		return false;
	}

	if (isCapture)
		m_swr = makeResampler(m_devFormat, m_devSampleRate, m_devChannels,
		                      AV_SAMPLE_FMT_S16, m_sampleRate, m_channels);
	else
		m_swr = makeResampler(AV_SAMPLE_FMT_S16, m_sampleRate, m_channels,
		                      m_devFormat, m_devSampleRate, m_devChannels);

	if (!m_swr)
	{
		if (error)
			*error = QStringLiteral("Could not set up the audio format converter");
		close();
		return false;
	}

	m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!m_event)
	{
		if (error)
			*error = QStringLiteral("Could not create the audio event");
		close();
		return false;
	}

	hr = m_client->SetEventHandle(m_event);
	if (FAILED(hr))
	{
		if (error)
			*error = QString("Could not attach the audio event: %1").arg(hrString(hr));
		close();
		return false;
	}

	hr = m_client->GetBufferSize(&m_bufferFrames);
	if (FAILED(hr))
	{
		if (error)
			*error = QString("Could not query the audio buffer size: %1").arg(hrString(hr));
		close();
		return false;
	}

	if (isCapture)
		hr = m_client->GetService(IID_IAudioCaptureClient, (void **)&m_capture);
	else
		hr = m_client->GetService(IID_IAudioRenderClient, (void **)&m_render);

	if (FAILED(hr))
	{
		if (error)
			*error = QString("Could not get the audio %1 service: %2")
			         .arg(isCapture ? "capture" : "render", hrString(hr));
		close();
		return false;
	}

	// Deliberately not pre-filling a render stream with silence here. It
	// would buy nothing - the engine plays silence for an empty buffer by
	// itself, and for a call the jitter buffer's own preroll has emptied it
	// again long before the first packet arrives - while costing a whole
	// buffer of added latency on every stream, and making the first write of
	// a tone wait for a buffer of silence to drain before anything is heard.

	hr = m_client->Start();
	if (FAILED(hr))
	{
		if (error)
			*error = QString("Could not start the audio stream: %1").arg(hrString(hr));
		close();
		return false;
	}
	m_started = true;

	qDebug() << "WASAPI:" << (isCapture ? "capture" : "playback") << "on" << device
	         << "- engine" << m_devSampleRate << "Hz" << m_devChannels << "ch"
	         << av_get_sample_fmt_name(m_devFormat)
	         << "<->" << m_sampleRate << "Hz" << m_channels << "ch s16"
	         << ", buffer" << m_bufferFrames << "frames"
	         << (m_platformAec ? ", platform echo cancellation requested" : "");

	return true;
}

// Turns one captured packet into our own format and distributes it between
// the caller's buffer and the spill buffer. Returns the new written count.
int WasapiBackend::appendConverted(const BYTE *deviceData, int deviceFrames, bool silent,
                                   qint16 *buf, int maxSamples, int written)
{
	const qint16 *src = nullptr;
	int samples = 0;

	if (!m_swr)
	{
		samples = deviceFrames * m_channels;
		if (silent)
		{
			m_convIn.resize(samples);
			memset(m_convIn.data(), 0, samples * sizeof(qint16));
			src = m_convIn.constData();
		}
		else
		{
			src = (const qint16 *)deviceData;
		}
	}
	else
	{
		// Worst case output length, allowing for whatever the resampler is
		// still holding on to.
		int outFrames = (int)av_rescale_rnd(swr_get_delay(m_swr, m_devSampleRate) + deviceFrames,
		                                    m_sampleRate, m_devSampleRate, AV_ROUND_UP);
		if (outFrames <= 0)
			return written;

		m_convIn.resize(outFrames * m_channels);

		QVector<quint8> silence;
		const quint8 *in = deviceData;
		if (silent)
		{
			silence.resize(deviceFrames * m_devBytesPerFrame);
			memset(silence.data(), 0, silence.size());
			in = silence.constData();
		}

		uint8_t *outPlanes[1] = { (uint8_t *)m_convIn.data() };
		const uint8_t *inPlanes[1] = { (const uint8_t *)in };

		int got = swr_convert(m_swr, outPlanes, outFrames, inPlanes, deviceFrames);
		if (got <= 0)
			return written;

		samples = got * m_channels;
		src = m_convIn.constData();
	}

	const int room = maxSamples - written;
	const int n = samples < room ? samples : room;

	if (n > 0)
		memcpy(buf + written, src, n * sizeof(qint16));

	if (samples > n)
	{
		int extra = samples - n;
		int base = m_spill.size();
		m_spill.resize(base + extra);
		memcpy(m_spill.data() + base, src + n, extra * sizeof(qint16));
	}

	return written + n;
}

int WasapiBackend::read(qint16 *buf, int maxSamples, int timeoutMs)
{
	if (!m_capture || !m_event)
		return -1;

	int written = 0;

	// Anything a previous read() could not fit goes out first.
	if (m_spillOffset < m_spill.size())
	{
		int avail = m_spill.size() - m_spillOffset;
		int n = avail < maxSamples ? avail : maxSamples;
		memcpy(buf, m_spill.constData() + m_spillOffset, n * sizeof(qint16));
		m_spillOffset += n;
		written += n;

		if (m_spillOffset >= m_spill.size())
		{
			m_spill.clear();
			m_spillOffset = 0;
		}

		if (written >= maxSamples)
			return written;
	}

	DWORD wait = WaitForSingleObject(m_event, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs);
	if (wait == WAIT_TIMEOUT)
		return written;
	if (wait != WAIT_OBJECT_0)
		return written > 0 ? written : -1;

	for (;;)
	{
		UINT32 packetFrames = 0;
		HRESULT hr = m_capture->GetNextPacketSize(&packetFrames);
		if (FAILED(hr))
			return written > 0 ? written : -1;
		if (packetFrames == 0)
			break;

		BYTE *data = nullptr;
		UINT32 frames = 0;
		DWORD flags = 0;
		hr = m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
		if (hr == AUDCLNT_S_BUFFER_EMPTY)
		{
			// A success code, so the buffer is still ours to give back even
			// though it holds nothing. Walking away without releasing it
			// makes every later GetBuffer fail with AUDCLNT_E_OUT_OF_ORDER,
			// which would kill capture for the rest of the call.
			m_capture->ReleaseBuffer(0);
			break;
		}
		if (FAILED(hr))
			return written > 0 ? written : -1;

		written = appendConverted(data, (int)frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0,
		                          buf, maxSamples, written);

		// The packet has to be released whole, which is why anything that did
		// not fit went to the spill buffer rather than being left behind.
		m_capture->ReleaseBuffer(frames);

		if (written >= maxSamples)
			break;
	}

	return written;
}

int WasapiBackend::write(const qint16 *buf, int nsamples, int timeoutMs)
{
	if (!m_render || !m_client || !m_event)
		return -1;

	const BYTE *deviceBytes = nullptr;
	int framesWanted = 0;

	if (!m_swr)
	{
		deviceBytes = (const BYTE *)buf;
		framesWanted = nsamples / m_channels;
	}
	else
	{
		int inFrames = nsamples / m_channels;
		int outFrames = (int)av_rescale_rnd(swr_get_delay(m_swr, m_sampleRate) + inFrames,
		                                    m_devSampleRate, m_sampleRate, AV_ROUND_UP);
		if (outFrames <= 0)
			return nsamples;

		m_convOut.resize(outFrames * m_devBytesPerFrame);

		uint8_t *outPlanes[1] = { (uint8_t *)m_convOut.data() };
		const uint8_t *inPlanes[1] = { (const uint8_t *)buf };

		int got = swr_convert(m_swr, outPlanes, outFrames, inPlanes, inFrames);
		if (got <= 0)
			return nsamples;

		deviceBytes = (const BYTE *)m_convOut.constData();
		framesWanted = got;
	}

	int framesDone = 0;

	// Deadline rather than a per-wait timeout. One WaitForSingleObject()
	// returns after a single device period, which is smaller than the block
	// the caller hands us, so filling a block normally takes several waits -
	// an earlier version of this allowed only one and silently dropped the
	// remainder of every block, which sounds like the tone being chopped up.
	// Subtracting tick counts is wrap safe as long as it is done in DWORD.
	const DWORD start = GetTickCount();

	while (framesDone < framesWanted)
	{
		UINT32 padding = 0;
		HRESULT hr = m_client->GetCurrentPadding(&padding);
		if (FAILED(hr))
			return framesDone > 0 ? nsamples : -1;

		UINT32 freeFrames = m_bufferFrames > padding ? m_bufferFrames - padding : 0;
		if (freeFrames == 0)
		{
			DWORD remaining;

			if (timeoutMs < 0)
			{
				remaining = INFINITE;
			}
			else
			{
				DWORD elapsed = GetTickCount() - start;
				if (elapsed >= (DWORD)timeoutMs)
					break;
				remaining = (DWORD)timeoutMs - elapsed;
			}

			DWORD wait = WaitForSingleObject(m_event, remaining);
			if (wait == WAIT_TIMEOUT)
				break;
			if (wait != WAIT_OBJECT_0)
				return framesDone > 0 ? nsamples : -1;
			continue;
		}

		UINT32 chunk = (UINT32)(framesWanted - framesDone);
		if (chunk > freeFrames)
			chunk = freeFrames;

		BYTE *data = nullptr;
		hr = m_render->GetBuffer(chunk, &data);
		if (FAILED(hr) || !data)
			return framesDone > 0 ? nsamples : -1;

		memcpy(data, deviceBytes + (size_t)framesDone * m_devBytesPerFrame,
		       (size_t)chunk * m_devBytesPerFrame);
		m_render->ReleaseBuffer(chunk, 0);

		framesDone += (int)chunk;
	}

	// Report against what the caller gave us, not against the post-conversion
	// frame count, since a short write here is a dropped tail either way.
	if (framesDone >= framesWanted)
		return nsamples;

	return (int)((qint64)nsamples * framesDone / (framesWanted > 0 ? framesWanted : 1));
}

void WasapiBackend::close()
{
	if (m_client && m_started)
	{
		// Stop() cuts off whatever is still in the buffer, which for a tone
		// means losing its last few tens of milliseconds. Give the device a
		// chance to play out what it has first - bounded, because a stream
		// that has stopped advancing must not hold up the call teardown.
		if (m_render && m_event)
		{
			const DWORD start = GetTickCount();
			const DWORD limit = 300;

			for (;;)
			{
				UINT32 padding = 0;
				if (FAILED(m_client->GetCurrentPadding(&padding)) || padding == 0)
					break;

				DWORD elapsed = GetTickCount() - start;
				if (elapsed >= limit)
					break;

				if (WaitForSingleObject(m_event, limit - elapsed) != WAIT_OBJECT_0)
					break;
			}
		}

		m_client->Stop();
		m_started = false;
	}

	if (m_capture)
	{
		m_capture->Release();
		m_capture = nullptr;
	}
	if (m_render)
	{
		m_render->Release();
		m_render = nullptr;
	}
	if (m_client)
	{
		m_client->Release();
		m_client = nullptr;
	}
	if (m_event)
	{
		CloseHandle(m_event);
		m_event = nullptr;
	}
	if (m_mixFormat)
	{
		CoTaskMemFree(m_mixFormat);
		m_mixFormat = nullptr;
	}
	if (m_swr)
	{
		swr_free(&m_swr);
		m_swr = nullptr;
	}

	m_spill.clear();
	m_spillOffset = 0;
	m_convIn.clear();
	m_convOut.clear();
	m_platformAec = false;

	if (m_comInitialized)
	{
		CoUninitialize();
		m_comInitialized = false;
	}
}

NativeAudioBackend *pica_create_wasapi_backend()
{
	return new WasapiBackend();
}

QList<MediaDeviceInfo> pica_enumerate_wasapi(enum MediaDeviceStreamDirection dir)
{
	QList<MediaDeviceInfo> result;

	// The settings dialog calls this from the GUI thread, which Qt has
	// already put into an apartment; the extra reference is still ours to
	// balance, hence the matching CoUninitialize() below.
	bool didInit = false;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr))
		didInit = true;
	else if (hr != RPC_E_CHANGED_MODE)
		return result;

	IMMDeviceEnumerator *devEnum = nullptr;
	hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
	                      IID_IMMDeviceEnumerator, (void **)&devEnum);
	if (FAILED(hr) || !devEnum)
	{
		qWarning() << "WASAPI: could not create the endpoint enumerator" << hrString(hr);
		if (didInit)
			CoUninitialize();
		return result;
	}

	int index = 0;

	// The system default first, under the same "default" name every other
	// driver in this codebase uses for it.
	//
	// Deliberately not named after a particular endpoint role. The same list
	// fills the call devices and the ring device, and "default" resolves to a
	// different endpoint for each: a call takes the communications one, a
	// ringtone the console one, so that the bell is heard on the speakers
	// rather than in a headset nobody is wearing yet. See resolveDevice().
	{
		MediaDeviceInfo d;
		d.device = QStringLiteral("default");
		d.humanReadable = (dir == PLAYBACK)
		                  ? QObject::tr("System default output")
		                  : QObject::tr("System default input");
		d.index = index++;
		result << d;
	}

	IMMDeviceCollection *collection = nullptr;
	hr = devEnum->EnumAudioEndpoints(dir == PLAYBACK ? eRender : eCapture,
	                                 DEVICE_STATE_ACTIVE, &collection);
	if (SUCCEEDED(hr) && collection)
	{
		UINT count = 0;
		collection->GetCount(&count);

		for (UINT i = 0; i < count; i++)
		{
			IMMDevice *dev = nullptr;
			if (FAILED(collection->Item(i, &dev)) || !dev)
				continue;

			LPWSTR id = nullptr;
			if (SUCCEEDED(dev->GetId(&id)) && id)
			{
				MediaDeviceInfo d;
				d.device = wideToQString(id);
				d.humanReadable = d.device;

				IPropertyStore *props = nullptr;
				if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props)
				{
					PROPVARIANT name;
					PropVariantInit(&name);
					if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) &&
					    name.vt == VT_LPWSTR)
					{
						d.humanReadable = wideToQString(name.pwszVal);
					}
					PropVariantClear(&name);
					props->Release();
				}

				d.index = index++;
				result << d;

				CoTaskMemFree(id);
			}

			dev->Release();
		}

		collection->Release();
	}

	devEnum->Release();

	if (didInit)
		CoUninitialize();

	return result;
}

#endif // Q_OS_WIN
