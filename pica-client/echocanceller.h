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

struct SpeexEchoState_;
struct SpeexPreprocessState_;

// Acoustic echo cancellation for a call, using speexdsp's frequency domain
// adaptive filter (MDF).
//
// One instance is shared by the two AudioDevice objects of a call - the
// playback one feeds it the far end signal it is about to hand to the sound
// card via pushFarEnd(), the capture one runs the microphone signal through
// processNearEnd() before encoding. Both run on their own thread, so every
// entry point here takes a lock.
//
// Because AudioVideoCallController tears a call down without joining those
// two threads (see stopAudioPipeline()), ownership is shared: hold this
// through a QSharedPointer so the object outlives whichever thread leaves
// last.
//
// Alignment between the two signals is left to the FIFO: in the steady state
// the playback side pushes samples at the same rate the capture side pulls
// them, and the backlog that builds up in the FIFO settles at exactly the
// sound card's buffer depth - which is also how far ahead of the speaker the
// playback side is running. What remains is the acoustic flight time from
// speaker to microphone plus any capture side buffering, and that is what the
// filter tail is for.
class EchoCanceller
{
public:
	// frameMs is the block the filter works on and must divide the audio
	// handed to processNearEnd(). tailMs is how much echo delay the filter
	// can model - it has to cover the sound card's output buffer plus the
	// room, and costs CPU and convergence time in proportion.
	EchoCanceller(int sampleRate, int frameMs = 10, int tailMs = 150);
	~EchoCanceller();

	bool isValid() const { return m_echo != nullptr; }
	int sampleRate() const { return m_sampleRate; }
	int frameSize() const { return m_frameSize; }

	// Forget the adapted filter and drop any buffered far end audio. Called
	// when a call starts, since the previous call's room response is not
	// worth keeping and a stale FIFO would start it off misaligned.
	void reset();

	// Playback side: the audio about to be written to the output device.
	void pushFarEnd(const qint16 *pcm, int nsamples);

	// Capture side: removes the echo of what pushFarEnd() was given, in
	// place. nsamples should be a whole number of frameSize() blocks; a
	// partial trailing block is passed through untouched.
	void processNearEnd(qint16 *pcm, int nsamples);

	// Rough measure of how well it is working, in dB - how much quieter the
	// output is than the microphone input while the far end is active. Zero
	// until enough far end audio has gone through to mean anything.
	double erle();

private:
	Q_DISABLE_COPY(EchoCanceller)

	int m_sampleRate;
	int m_frameSize;
	int m_filterLength;

	// speexdsp state. The echo canceller proper, plus a preprocessor whose
	// only job here is the residual echo suppression that the adaptive
	// filter alone cannot do (SPEEX_PREPROCESS_SET_ECHO_STATE).
	SpeexEchoState_ *m_echo;
	SpeexPreprocessState_ *m_preprocess;

	// Guards the speex state, which both directions reach into: the capture
	// thread through speex_echo_cancellation(), and reset() from whichever
	// thread starts the call.
	QMutex m_procMutex;

	// Far end FIFO, written by the playback thread and drained by the
	// capture thread.
	QMutex m_farMutex;
	QVector<qint16> m_farFifo;
	int m_farHead;

	// Upper bound on the FIFO, in samples. Reached only if the two sound
	// cards' clocks drift apart or playback outruns capture; past this the
	// oldest audio is dropped, since keeping it would mean the reference
	// signal lagging further and further behind the echo it is meant to
	// explain.
	int m_farCapacity;

	// Running sums of near end and output energy, for erle().
	double m_nearEnergy;
	double m_outEnergy;
	QMutex m_statMutex;

	// Scratch far end block, reused by processNearEnd().
	QVector<qint16> m_farBlock;

	bool popFarEnd(qint16 *dst, int nsamples);
};

typedef QSharedPointer<EchoCanceller> EchoCancellerPtr;

#endif // ECHOCANCELLER_H
