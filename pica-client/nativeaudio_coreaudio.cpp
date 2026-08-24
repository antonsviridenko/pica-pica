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

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

#include "audioring.h"

#include <QDebug>
#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>

#include <time.h>
#include <string.h>

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

// Bus numbering for an output-type audio unit, which is what both the
// VoiceProcessingIO and the plain HAL output unit are: bus 0 faces the
// speaker, bus 1 the microphone.
static const AudioUnitElement kOutputBus = 0;
static const AudioUnitElement kInputBus = 1;

// Largest slice a unit will ever hand us. The default is 1156 frames; ask for
// a round number above it and size the scratch buffers to match.
static const UInt32 kMaxFramesPerSlice = 4096;

static QString cfStringToQString(CFStringRef s)
{
	if (!s)
		return QString();

	CFIndex length = CFStringGetLength(s);
	CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
	QVector<char> buf(maxSize);

	if (!CFStringGetCString(s, buf.data(), maxSize, kCFStringEncodingUTF8))
		return QString();

	return QString::fromUtf8(buf.constData());
}

static QString osStatusString(OSStatus err)
{
	return QString::number((int)err);
}

// Number of channels a device offers on one scope. Zero means it does not do
// that direction at all, which is how an input-only or output-only device is
// told apart.
static int deviceChannelCount(AudioDeviceID dev, bool input)
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyStreamConfiguration;
	addr.mScope = input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
	addr.mElement = kAudioObjectPropertyElementMaster;

	UInt32 size = 0;
	if (AudioObjectGetPropertyDataSize(dev, &addr, 0, nullptr, &size) != noErr || size == 0)
		return 0;

	QVector<char> raw(size);
	AudioBufferList *list = (AudioBufferList *)raw.data();
	if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, list) != noErr)
		return 0;

	int channels = 0;
	for (UInt32 i = 0; i < list->mNumberBuffers; i++)
		channels += list->mBuffers[i].mNumberChannels;

	return channels;
}

static QString deviceUid(AudioDeviceID dev)
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyDeviceUID;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMaster;

	CFStringRef uid = nullptr;
	UInt32 size = sizeof(uid);
	if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &uid) != noErr || !uid)
		return QString();

	QString result = cfStringToQString(uid);
	CFRelease(uid);
	return result;
}

static QString deviceName(AudioDeviceID dev)
{
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioObjectPropertyName;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMaster;

	CFStringRef name = nullptr;
	UInt32 size = sizeof(name);
	if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &name) != noErr || !name)
		return QString();

	QString result = cfStringToQString(name);
	CFRelease(name);
	return result;
}

static QVector<AudioDeviceID> allDevices()
{
	QVector<AudioDeviceID> devices;

	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioHardwarePropertyDevices;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMaster;

	UInt32 size = 0;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr)
		return devices;

	devices.resize(size / sizeof(AudioDeviceID));
	if (devices.isEmpty())
		return devices;

	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data()) != noErr)
		devices.clear();

	return devices;
}

// Looks up a device by the UID string enumerate() handed out.
static AudioDeviceID deviceForUid(const QString &uid, bool input)
{
	QVector<AudioDeviceID> devices = allDevices();

	for (int i = 0; i < devices.size(); i++)
	{
		if (deviceChannelCount(devices[i], input) <= 0)
			continue;
		if (deviceUid(devices[i]) == uid)
			return devices[i];
	}

	return kAudioObjectUnknown;
}

// An output-type audio unit and the rings that connect its real time callbacks
// to the AudioDevice loops.
//
// Two shapes of it exist, and the difference is the whole reason this file is
// here. A voice unit is kAudioUnitSubType_VoiceProcessingIO: it renders and
// captures at once and subtracts what it is about to play from what it just
// heard, which is macOS's acoustic echo canceller and is better than anything
// we could run in process. Because that only works when both directions go
// through the same unit, and AudioDevice keeps one object per direction on its
// own thread, the voice unit is a refcounted singleton the two of them share.
//
// A plain unit is kAudioUnitSubType_HALOutput, output only, no voice
// processing, one per caller. Ringtones and earpiece tones use it: they are
// not part of a call, and opening a voice unit to play one would switch the
// microphone on, prompt for permission and light the recording indicator.
//
// One consequence of the singleton worth knowing about: an audio unit is
// bound to one device, so capture and playback cannot be on different sound
// cards while the AEC is in use. When the two selections disagree, the one
// that got there first wins and a warning is logged.
class CoreAudioUnit
{
public:
	// The shared voice unit. forCapture says which direction is attaching;
	// release() must later be called with the same value.
	static CoreAudioUnit *acquireVoice(const QString &device, int sampleRate, int channels,
	                                   bool forCapture, QString *error);

	// A private output only unit with no voice processing. The caller owns
	// it and releases it the same way.
	static CoreAudioUnit *createPlain(const QString &device, int sampleRate, int channels,
	                                  QString *error);

	void release(bool forCapture);

	AudioRing captureRing;
	AudioRing playbackRing;

	bool voiceProcessing() const { return m_voiceProcessing; }

private:
	CoreAudioUnit(int sampleRate, int channels, bool voiceProcessing,
	              bool wantInput, bool wantOutput)
		: captureRing(sampleRate * 2), playbackRing(sampleRate * 2),
		  m_unit(nullptr), m_sampleRate(sampleRate), m_channels(channels),
		  m_voiceProcessing(voiceProcessing), m_wantInput(wantInput),
		  m_wantOutput(wantOutput), m_captureRefs(0), m_playbackRefs(0),
		  m_started(false)
	{
	}

	~CoreAudioUnit();

	bool setUp(const QString &device, QString *error);
	void tearDown();

	static OSStatus inputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
	                              const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
	                              UInt32 inNumberFrames, AudioBufferList *ioData);
	static OSStatus renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
	                               const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
	                               UInt32 inNumberFrames, AudioBufferList *ioData);

	AudioUnit m_unit;
	int m_sampleRate;
	int m_channels;
	bool m_voiceProcessing;
	bool m_wantInput;
	bool m_wantOutput;

	QString m_device;

	int m_captureRefs;
	int m_playbackRefs;
	bool m_started;

	// Scratch buffers used only from the audio callbacks, sized once at set
	// up time so the real time threads never allocate.
	QVector<float> m_inputScratch;
	QVector<qint16> m_pcmScratch;
	QVector<char> m_inputListStorage;

	// Guards the singleton below and the two refcounts. Plain units are
	// reached by one thread only but take it too, which costs nothing.
	static QMutex s_mutex;
	static CoreAudioUnit *s_voiceInstance;
};

QMutex CoreAudioUnit::s_mutex;
CoreAudioUnit *CoreAudioUnit::s_voiceInstance = nullptr;

CoreAudioUnit::~CoreAudioUnit()
{
	tearDown();
}

bool CoreAudioUnit::setUp(const QString &device, QString *error)
{
	AudioComponentDescription desc;
	desc.componentType = kAudioUnitType_Output;
	desc.componentSubType = m_voiceProcessing ? kAudioUnitSubType_VoiceProcessingIO
	                                          : kAudioUnitSubType_HALOutput;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
	if (!comp)
	{
		if (error)
			*error = m_voiceProcessing
			         ? QStringLiteral("The VoiceProcessingIO audio unit is not available")
			         : QStringLiteral("The HAL output audio unit is not available");
		return false;
	}

	OSStatus err = AudioComponentInstanceNew(comp, &m_unit);
	if (err != noErr || !m_unit)
	{
		if (error)
			*error = QString("Could not create the audio unit: %1").arg(osStatusString(err));
		m_unit = nullptr;
		return false;
	}

	UInt32 one = 1;
	UInt32 zero = 0;

	err = AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input,
	                           kInputBus, m_wantInput ? &one : &zero, sizeof(one));
	if (err != noErr && m_wantInput)
	{
		if (error)
			*error = QString("Could not enable audio input: %1").arg(osStatusString(err));
		return false;
	}

	err = AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output,
	                           kOutputBus, m_wantOutput ? &one : &zero, sizeof(one));
	if (err != noErr && m_wantOutput)
	{
		if (error)
			*error = QString("Could not enable audio output: %1").arg(osStatusString(err));
		return false;
	}

	// Bind to a specific device when one was asked for. Left alone, the unit
	// follows the system default input and output, which is what most users
	// want and what keeps working when they unplug a headset mid-call.
	if (!device.isEmpty() && device != QLatin1String("default"))
	{
		AudioDeviceID devId = deviceForUid(device, m_wantInput);
		if (devId == kAudioObjectUnknown)
			devId = deviceForUid(device, !m_wantInput);

		if (devId != kAudioObjectUnknown)
		{
			err = AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_CurrentDevice,
			                           kAudioUnitScope_Global, kOutputBus, &devId, sizeof(devId));
			if (err != noErr)
				qWarning() << "CoreAudio: could not bind the audio unit to device" << device
				           << osStatusString(err) << "- using the system default";
		}
		else
		{
			qWarning() << "CoreAudio: device" << device << "not found - using the system default";
		}
	}
	m_device = device;

	UInt32 maxFrames = kMaxFramesPerSlice;
	AudioUnitSetProperty(m_unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
	                     0, &maxFrames, sizeof(maxFrames));

	// The client side format on both buses. The unit's own converters deal
	// with whatever the hardware is really running at, including the sample
	// rate, so we can ask for the 48 kHz mono the rest of the call uses.
	// Float32 because that is what these units work in natively; going
	// through Int16 here would only add a conversion the unit undoes.
	AudioStreamBasicDescription fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.mSampleRate = m_sampleRate;
	fmt.mFormatID = kAudioFormatLinearPCM;
	fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	fmt.mFramesPerPacket = 1;
	fmt.mChannelsPerFrame = m_channels;
	fmt.mBitsPerChannel = 32;
	fmt.mBytesPerFrame = 4 * m_channels;
	fmt.mBytesPerPacket = fmt.mBytesPerFrame;

	if (m_wantInput)
	{
		// Output scope of the input bus is what AudioUnitRender() gives us.
		err = AudioUnitSetProperty(m_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
		                           kInputBus, &fmt, sizeof(fmt));
		if (err != noErr)
		{
			if (error)
				*error = QString("Could not set the capture format: %1").arg(osStatusString(err));
			return false;
		}
	}

	if (m_wantOutput)
	{
		// Input scope of the output bus is what our render callback supplies.
		err = AudioUnitSetProperty(m_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
		                           kOutputBus, &fmt, sizeof(fmt));
		if (err != noErr)
		{
			if (error)
				*error = QString("Could not set the playback format: %1").arg(osStatusString(err));
			return false;
		}
	}

	if (m_voiceProcessing)
	{
		// Make sure the echo cancellation and the rest of the voice
		// processing is actually switched on. It is by default, but a bypass
		// left set by an earlier instance of the unit would silently cost us
		// the whole point of using it.
		UInt32 bypass = 0;
		AudioUnitSetProperty(m_unit, kAUVoiceIOProperty_BypassVoiceProcessing,
		                     kAudioUnitScope_Global, 0, &bypass, sizeof(bypass));

		// Leave the unit's automatic gain control off, for the same reason
		// the speexdsp path leaves speex's off: a moving gain in front of a
		// call that has no level control anywhere else is more trouble than
		// it is worth.
		UInt32 agc = 0;
		AudioUnitSetProperty(m_unit, kAUVoiceIOProperty_VoiceProcessingEnableAGC,
		                     kAudioUnitScope_Global, 0, &agc, sizeof(agc));
	}

	AURenderCallbackStruct cb;

	if (m_wantInput)
	{
		cb.inputProc = &CoreAudioUnit::inputCallback;
		cb.inputProcRefCon = this;
		err = AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_SetInputCallback,
		                           kAudioUnitScope_Global, kInputBus, &cb, sizeof(cb));
		if (err != noErr)
		{
			if (error)
				*error = QString("Could not install the capture callback: %1").arg(osStatusString(err));
			return false;
		}
	}

	if (m_wantOutput)
	{
		cb.inputProc = &CoreAudioUnit::renderCallback;
		cb.inputProcRefCon = this;
		err = AudioUnitSetProperty(m_unit, kAudioUnitProperty_SetRenderCallback,
		                           kAudioUnitScope_Input, kOutputBus, &cb, sizeof(cb));
		if (err != noErr)
		{
			if (error)
				*error = QString("Could not install the playback callback: %1").arg(osStatusString(err));
			return false;
		}
	}

	m_inputScratch.resize(kMaxFramesPerSlice * m_channels);
	m_pcmScratch.resize(kMaxFramesPerSlice * m_channels);
	m_inputListStorage.resize(sizeof(AudioBufferList) + sizeof(AudioBuffer));

	err = AudioUnitInitialize(m_unit);
	if (err != noErr)
	{
		if (error)
			*error = QString("Could not initialise the audio unit: %1").arg(osStatusString(err));
		return false;
	}

	err = AudioOutputUnitStart(m_unit);
	if (err != noErr)
	{
		if (error)
			*error = QString("Could not start the audio unit: %1").arg(osStatusString(err));
		return false;
	}
	m_started = true;

	qDebug() << "CoreAudio:" << (m_voiceProcessing ? "VoiceProcessingIO" : "HALOutput")
	         << "started on" << (m_device.isEmpty() ? QStringLiteral("default") : m_device)
	         << m_sampleRate << "Hz" << m_channels << "ch";

	return true;
}

void CoreAudioUnit::tearDown()
{
	if (!m_unit)
		return;

	if (m_started)
	{
		AudioOutputUnitStop(m_unit);
		m_started = false;
	}

	AudioUnitUninitialize(m_unit);
	AudioComponentInstanceDispose(m_unit);
	m_unit = nullptr;
}

OSStatus CoreAudioUnit::inputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                      const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                      UInt32 inNumberFrames, AudioBufferList *ioData)
{
	Q_UNUSED(ioData)

	CoreAudioUnit *self = (CoreAudioUnit *)inRefCon;
	if (!self->m_unit || inNumberFrames == 0 || inNumberFrames > kMaxFramesPerSlice)
		return noErr;

	AudioBufferList *list = (AudioBufferList *)self->m_inputListStorage.data();
	list->mNumberBuffers = 1;
	list->mBuffers[0].mNumberChannels = self->m_channels;
	list->mBuffers[0].mDataByteSize = inNumberFrames * self->m_channels * sizeof(float);
	list->mBuffers[0].mData = self->m_inputScratch.data();

	OSStatus err = AudioUnitRender(self->m_unit, ioActionFlags, inTimeStamp,
	                               inBusNumber, inNumberFrames, list);
	if (err != noErr)
		return err;

	const int samples = inNumberFrames * self->m_channels;
	const float *src = self->m_inputScratch.constData();
	qint16 *dst = self->m_pcmScratch.data();

	for (int i = 0; i < samples; i++)
	{
		float v = src[i] * 32767.0f;
		if (v > 32767.0f) v = 32767.0f;
		else if (v < -32768.0f) v = -32768.0f;
		dst[i] = (qint16)v;
	}

	// A full ring means the capture loop has stalled; the ring counts the
	// overrun and drops what does not fit, which is the right trade for a
	// live call.
	self->captureRing.write(dst, samples);

	return noErr;
}

OSStatus CoreAudioUnit::renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                       const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                       UInt32 inNumberFrames, AudioBufferList *ioData)
{
	Q_UNUSED(ioActionFlags)
	Q_UNUSED(inTimeStamp)
	Q_UNUSED(inBusNumber)

	CoreAudioUnit *self = (CoreAudioUnit *)inRefCon;
	if (!ioData || ioData->mNumberBuffers < 1)
		return noErr;

	const int samples = inNumberFrames * self->m_channels;
	if (samples > self->m_pcmScratch.size())
	{
		for (UInt32 b = 0; b < ioData->mNumberBuffers; b++)
			memset(ioData->mBuffers[b].mData, 0, ioData->mBuffers[b].mDataByteSize);
		return noErr;
	}

	// Silence for anything the playback loop has not produced yet. Between
	// calls, and whenever the peer is quiet, this is all the callback does -
	// and a voice unit still wants it, because its echo canceller needs to
	// know that nothing is being played.
	qint16 *pcm = self->m_pcmScratch.data();
	self->playbackRing.readOrSilence(pcm, samples);

	float *out = (float *)ioData->mBuffers[0].mData;
	for (int i = 0; i < samples; i++)
		out[i] = pcm[i] / 32768.0f;

	ioData->mBuffers[0].mDataByteSize = samples * sizeof(float);

	// A non-interleaved layout would give us one buffer per channel; the
	// format set in setUp() is packed, so anything past the first is not ours
	// to fill and is zeroed to be safe.
	for (UInt32 b = 1; b < ioData->mNumberBuffers; b++)
		memset(ioData->mBuffers[b].mData, 0, ioData->mBuffers[b].mDataByteSize);

	return noErr;
}

CoreAudioUnit *CoreAudioUnit::acquireVoice(const QString &device, int sampleRate, int channels,
                                           bool forCapture, QString *error)
{
	QMutexLocker locker(&s_mutex);

	if (!s_voiceInstance)
	{
		CoreAudioUnit *unit = new CoreAudioUnit(sampleRate, channels, true, true, true);
		if (!unit->setUp(device, error))
		{
			delete unit;
			return nullptr;
		}
		s_voiceInstance = unit;
	}
	else
	{
		if (s_voiceInstance->m_sampleRate != sampleRate || s_voiceInstance->m_channels != channels)
		{
			// Both directions of a call are negotiated together, so this
			// would mean two calls at once, which nothing above supports.
			if (error)
				*error = QStringLiteral("The voice audio unit is already running at another format");
			return nullptr;
		}

		if (s_voiceInstance->m_device != device)
		{
			qWarning() << "CoreAudio: the voice unit is bound to" << s_voiceInstance->m_device
			           << "so" << device << "cannot also be used - see the note on CoreAudioUnit";
		}
	}

	if (forCapture)
	{
		s_voiceInstance->captureRing.clear();
		s_voiceInstance->m_captureRefs++;
	}
	else
	{
		s_voiceInstance->playbackRing.clear();
		s_voiceInstance->m_playbackRefs++;
	}

	return s_voiceInstance;
}

CoreAudioUnit *CoreAudioUnit::createPlain(const QString &device, int sampleRate, int channels,
                                          QString *error)
{
	CoreAudioUnit *unit = new CoreAudioUnit(sampleRate, channels, false, false, true);
	if (!unit->setUp(device, error))
	{
		delete unit;
		return nullptr;
	}

	unit->m_playbackRefs = 1;
	return unit;
}

void CoreAudioUnit::release(bool forCapture)
{
	QMutexLocker locker(&s_mutex);

	if (forCapture)
	{
		if (m_captureRefs > 0)
			m_captureRefs--;
	}
	else
	{
		if (m_playbackRefs > 0)
			m_playbackRefs--;
	}

	if (m_captureRefs == 0 && m_playbackRefs == 0)
	{
		if (s_voiceInstance == this)
			s_voiceInstance = nullptr;
		delete this;
	}
}

// Waits a millisecond. The units fill and drain their rings from threads we
// do not own and have nothing to signal on, so there is nothing to block on
// but the clock.
static void sleepOneMs()
{
	struct timespec ts = { 0, 1000000 };
	nanosleep(&ts, nullptr);
}

class CoreAudioBackend : public NativeAudioBackend
{
public:
	CoreAudioBackend()
		: m_unit(nullptr), m_isCapture(false), m_channels(1), m_platformAec(false)
	{
	}

	~CoreAudioBackend() override
	{
		close();
	}

	QString apiName() const override { return QStringLiteral("coreaudio"); }

	bool openCapture(const QString &device, int sampleRate, int channels,
	                 NativeAudioMode mode, QString *error) override
	{
		close();

		// There is no plain capture path: the only reason this backend
		// captures at all is the call, and the call always wants the voice
		// unit.
		Q_UNUSED(mode)

		m_unit = CoreAudioUnit::acquireVoice(device, sampleRate, channels, true, error);
		m_isCapture = true;
		m_channels = channels;
		m_platformAec = (m_unit != nullptr);

		return m_unit != nullptr;
	}

	bool openPlayback(const QString &device, int sampleRate, int channels,
	                  NativeAudioMode mode, QString *error) override
	{
		close();

		if (mode == NativeAudioVoiceCall)
			m_unit = CoreAudioUnit::acquireVoice(device, sampleRate, channels, false, error);
		else
			m_unit = CoreAudioUnit::createPlain(device, sampleRate, channels, error);

		m_isCapture = false;
		m_channels = channels;
		m_platformAec = false;

		return m_unit != nullptr;
	}

	int read(qint16 *buf, int maxSamples, int timeoutMs) override
	{
		if (!m_unit || !m_isCapture)
			return -1;

		int waited = 0;
		for (;;)
		{
			int avail = m_unit->captureRing.available();
			if (avail > 0)
			{
				int n = avail < maxSamples ? avail : maxSamples;
				return m_unit->captureRing.read(buf, n);
			}

			if (timeoutMs >= 0 && waited >= timeoutMs)
				return 0;

			sleepOneMs();
			waited++;
		}
	}

	int write(const qint16 *buf, int nsamples, int timeoutMs) override
	{
		if (!m_unit || m_isCapture)
			return -1;

		int done = 0;
		int waited = 0;

		while (done < nsamples)
		{
			done += m_unit->playbackRing.write(buf + done, nsamples - done);

			if (done >= nsamples)
				break;

			if (timeoutMs >= 0 && waited >= timeoutMs)
				break;

			sleepOneMs();
			waited++;
		}

		return done;
	}

	void close() override
	{
		if (m_unit)
		{
			m_unit->release(m_isCapture);
			m_unit = nullptr;
		}
		m_platformAec = false;
	}

	// The whole reason this backend exists. The voice unit's canceller is
	// unconditional once the unit is running and not bypassed.
	bool hasPlatformEchoCancellation() const override { return m_platformAec; }

	QString formatDescription() const override
	{
		if (!m_unit)
			return QString();

		return QString("CoreAudio %1 %2, %3 ch%4")
		       .arg(m_isCapture ? "in" : "out")
		       .arg(m_unit->voiceProcessing() ? "VoiceProcessingIO" : "HALOutput")
		       .arg(m_channels)
		       .arg(m_platformAec ? ", platform AEC" : "");
	}

private:
	CoreAudioUnit *m_unit;
	bool m_isCapture;
	int m_channels;
	bool m_platformAec;
};

NativeAudioBackend *pica_create_coreaudio_backend()
{
	return new CoreAudioBackend();
}

QList<MediaDeviceInfo> pica_enumerate_coreaudio(enum MediaDeviceStreamDirection dir)
{
	QList<MediaDeviceInfo> result;
	int index = 0;

	// The system default first, under the same "default" name every other
	// driver in this codebase uses for it. On macOS it is also the only
	// selection that lets the voice unit follow the user switching headsets
	// during a call.
	{
		MediaDeviceInfo d;
		d.device = QStringLiteral("default");
		d.humanReadable = (dir == PLAYBACK)
		                  ? QObject::tr("System default output")
		                  : QObject::tr("System default input");
		d.index = index++;
		result << d;
	}

	const bool wantInput = (dir != PLAYBACK);
	QVector<AudioDeviceID> devices = allDevices();

	for (int i = 0; i < devices.size(); i++)
	{
		if (deviceChannelCount(devices[i], wantInput) <= 0)
			continue;

		QString uid = deviceUid(devices[i]);
		if (uid.isEmpty())
			continue;

		MediaDeviceInfo d;
		d.device = uid;
		d.humanReadable = deviceName(devices[i]);
		if (d.humanReadable.isEmpty())
			d.humanReadable = uid;
		d.index = index++;

		result << d;
	}

	return result;
}

#endif // Q_OS_MACOS
