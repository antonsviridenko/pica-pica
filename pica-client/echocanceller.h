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
#ifndef ECHOCANCELLER_H
#define ECHOCANCELLER_H

#include <QString>
#include <QMutex>
#include <QVector>
#include <QSharedPointer>

struct EchoCancellerPrivate;
struct SwrContext;

// Where the acoustic echo of a call is taken out of the microphone signal.
// One of these three, never two at once: running a second canceller over an
// already cancelled signal gives it a near silent reference to chase and it
// chews holes in the speech.
enum EchoCancellationMode
{
	// EchoCanceller below - WebRTC's AEC3, in process.
	EchoCancellationOwn,

	// Whatever the operating system or sound server does below us, which is
	// better where it exists: it sees the real speaker signal and the real
	// clock. Not offered everywhere - see
	// AudioDevice::PlatformEchoCancellationAvailable().
	EchoCancellationPlatform,

	// Nothing cancels. Correct with a headset, and the only honest answer
	// when both of the above make things worse.
	EchoCancellationNone
};

// The "audio.echo_cancellation" setting. Both must be called from a thread
// that may touch the settings database - see AudioDevice::SetLinuxDriverName()
// for why the audio threads may not.
//
// Nothing stored yet means the default for this platform: the platform's own
// where there is one, ours otherwise. The setting this replaced
// ("audio.echo_cancel", a plain on/off) is still honoured when it is all
// there is, so an existing install that had it switched off stays that way.
EchoCancellationMode loadEchoCancellationSetting();
void storeEchoCancellationSetting(EchoCancellationMode mode);

// Acoustic echo cancellation for a call, using WebRTC's AEC3 through the
// audio processing module.
//
// One instance is shared by the two AudioDevice objects of a call - the
// playback one feeds it the far end signal it is about to hand to the sound
// card via pushFarEnd(), the capture one runs the microphone signal through
// processNearEnd() before encoding. Both run on their own thread, which is
// what AEC3 expects: one render thread, one capture thread.
//
// Because AudioVideoCallController tears a call down without joining those
// two threads (see stopAudioPipeline()), ownership is shared: hold this
// through a QSharedPointer so the object outlives whichever thread leaves
// last.
//
// One per call, built when the call starts and dropped when it ends. There is
// nothing worth carrying over: the previous call's room response is not this
// call's, and the module is cheap enough to build.
//
// Nothing here tries to line the two signals up. AEC3 has its own delay
// estimator and a render buffer to hold the far end audio until the echo of
// it arrives, which is the whole reason it copes with an application that
// hands over playback audio some way ahead of the speaker actually making a
// sound - and with that lead changing when the sound card's buffer does.
class EchoCanceller
{
public:
	// sampleRate must be one of AEC3's native rates - 8000, 16000, 32000 or
	// 48000. Anything else leaves isValid() false rather than silently
	// processing at the wrong rate; the caller then runs without
	// cancellation.
	explicit EchoCanceller(int sampleRate);
	~EchoCanceller();

	bool isValid() const;
	int sampleRate() const { return m_sampleRate; }

	// The block AEC3 works on: 10ms, always. Audio handed to
	// processNearEnd() should be a whole number of these.
	int frameSize() const { return m_frameSize; }

	// Playback side: the audio about to be written to the output device.
	//
	// srcRate is what pcm is sampled at, and may differ from sampleRate():
	// we capture at a rate of our choosing but play back at whatever the peer
	// announced, so a call with a build that negotiates a different rate ends
	// up with the two directions disagreeing. Passing 0 means "same as ours".
	// Anything else is resampled on the way in.
	void pushFarEnd(const qint16 *pcm, int nsamples, int srcRate = 0);

	// Capture side: removes the echo of what pushFarEnd() was given, in
	// place. nsamples should be a whole number of frameSize() blocks; a
	// partial trailing block is passed through untouched.
	void processNearEnd(qint16 *pcm, int nsamples);

	// Echo return loss enhancement in dB, as AEC3 measures it: how much
	// quieter the echo is after cancellation than before. Zero until the
	// filter has had far end audio to work on.
	//
	// Only meaningful while the far end is actually talking, which is what
	// farEndLevelDb() is for: with nothing coming out of the speaker there is
	// no echo to remove and the figure means nothing.
	double erle();

	// Recent signal levels in dBFS, averaged over a few seconds.
	// nearEndLevelDb() is the microphone before cancellation - a floor-level
	// reading there means the capture path is dead and nothing else in the
	// numbers is worth reading.
	double nearEndLevelDb();
	double farEndLevelDb();

private:
	Q_DISABLE_COPY(EchoCanceller)

	int m_sampleRate;
	int m_frameSize;

	// The audio processing module and the stream configurations handed to it,
	// kept out of this header so that the WebRTC headers - which want C++17
	// and drag in abseil - are only seen by echocanceller.cpp.
	EchoCancellerPrivate *m_d;

	// Guards the render direction: the far end accumulator, the resampler,
	// and ProcessReverseStream(). Only the playback thread takes it.
	QMutex m_renderMutex;

	// Guards the capture direction: ProcessStream() and the statistics read
	// alongside it. Taken by the capture thread, and by whoever calls the
	// erle() accessor.
	QMutex m_captureMutex;

	// Far end audio that did not fill a whole 10ms block, waiting for the
	// samples that will complete it.
	QVector<qint16> m_farPending;

	// Rate conversion for a far end that is not running at our rate, built on
	// first use and rebuilt if the rate changes.
	SwrContext *m_farSwr;
	int m_farSwrRate;
	QVector<qint16> m_farScratch;

	// Exponentially averaged mean square of the near end (microphone) and of
	// the far end reference, for the level readings. Per sample rather than
	// per call, so they stay comparable whatever block size the capture path
	// hands us.
	double m_nearEnergy;
	double m_farEnergy;
	QMutex m_statMutex;

	// Drives the periodic qDebug() in processNearEnd(). Counted in samples
	// rather than wall clock so the report interval does not depend on how
	// punctually the capture thread is scheduled. Guarded by m_statMutex.
	qint64 m_processedSamples;
	qint64 m_nextReportSamples;

	// 10ms blocks handed to each direction. AEC3 lines the two streams up by
	// counting API calls, not by any clock, so the gap between these is what
	// its alignment is measured in. In a healthy call it settles at the depth
	// of the sound card's output buffer and stays there; a gap that keeps
	// growing or shrinking means capture and playback are running off
	// different clocks and the alignment is being dragged out from under the
	// filter. Guarded by m_statMutex.
	qint64 m_renderBlocks;
	qint64 m_captureBlocks;

	// How often the canceller reports how it is doing, in seconds.
	static const int kReportIntervalSec = 2;

	// True if rate is one of the rates AEC3's integer interface accepts.
	static bool isNativeRate(int rate);
};

typedef QSharedPointer<EchoCanceller> EchoCancellerPtr;

#endif // ECHOCANCELLER_H
