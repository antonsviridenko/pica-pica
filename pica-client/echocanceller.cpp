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

#include <QDebug>
#include <QMutexLocker>
#include <cmath>
#include <cstring>

extern "C" {
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
}

EchoCanceller::EchoCanceller(int sampleRate, int frameMs, int tailMs)
	: m_sampleRate(sampleRate > 0 ? sampleRate : 48000),
	  m_frameSize(0),
	  m_filterLength(0),
	  m_echo(nullptr),
	  m_preprocess(nullptr),
	  m_farHead(0),
	  m_farCapacity(0),
	  m_nearEnergy(0.0),
	  m_outEnergy(0.0)
{
	if (frameMs <= 0)
		frameMs = 10;
	if (tailMs < frameMs)
		tailMs = frameMs;

	m_frameSize = (m_sampleRate * frameMs) / 1000;
	m_filterLength = (m_sampleRate * tailMs) / 1000;

	// speexdsp's MDF works in blocks of frame_size and wants the filter to
	// be a whole number of them.
	if (m_frameSize <= 0)
		m_frameSize = 480;
	m_filterLength = ((m_filterLength + m_frameSize - 1) / m_frameSize) * m_frameSize;

	// Room for the sound card's output buffer plus a healthy margin. See the
	// comment on m_farCapacity in the header for what overflowing it means.
	m_farCapacity = m_sampleRate; // one second

	SpeexEchoState *echo = speex_echo_state_init(m_frameSize, m_filterLength);
	if (!echo)
	{
		qWarning() << "Could not create speex echo canceller state";
		return;
	}

	int rate = m_sampleRate;
	speex_echo_ctl(echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);

	SpeexPreprocessState *pre = speex_preprocess_state_init(m_frameSize, m_sampleRate);
	if (pre)
	{
		// The adaptive filter removes the part of the echo it can model; what
		// is left over - non-linearities in a cheap speaker, the tail of the
		// room past m_filterLength - has to be suppressed statistically, and
		// that is what the preprocessor's echo suppressor does with the
		// residual estimate the echo state hands it.
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_STATE, echo);

		int suppress = -40;
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &suppress);
		int suppressActive = -15;
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &suppressActive);

		// Leave denoise on - it costs little next to the filter and keeps the
		// residual suppressor's noise estimate honest. AGC stays off: the
		// call has no level control anywhere else, and having one end quietly
		// ride the gain makes the echo path non-stationary, which is the one
		// thing an adaptive filter cannot follow.
		int on = 1, off = 0;
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_DENOISE, &on);
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_AGC, &off);
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_VAD, &off);
	}
	else
	{
		qWarning() << "Could not create speex preprocessor - running without residual echo suppression";
	}

	m_echo = reinterpret_cast<SpeexEchoState_ *>(echo);
	m_preprocess = reinterpret_cast<SpeexPreprocessState_ *>(pre);

	m_farBlock.resize(m_frameSize);
	m_farFifo.reserve(m_farCapacity);

	qDebug() << "Echo canceller:" << m_sampleRate << "Hz, frame" << m_frameSize
	         << "samples, tail" << m_filterLength << "samples ("
	         << (m_filterLength * 1000 / m_sampleRate) << "ms )";
}

EchoCanceller::~EchoCanceller()
{
	// Order matters: the preprocessor holds a pointer to the echo state.
	if (m_preprocess)
		speex_preprocess_state_destroy(reinterpret_cast<SpeexPreprocessState *>(m_preprocess));
	if (m_echo)
		speex_echo_state_destroy(reinterpret_cast<SpeexEchoState *>(m_echo));
}

void EchoCanceller::reset()
{
	{
		QMutexLocker locker(&m_procMutex);
		if (m_echo)
			speex_echo_state_reset(reinterpret_cast<SpeexEchoState *>(m_echo));
	}

	{
		QMutexLocker locker(&m_farMutex);
		m_farFifo.clear();
		m_farHead = 0;
	}

	{
		QMutexLocker locker(&m_statMutex);
		m_nearEnergy = 0.0;
		m_outEnergy = 0.0;
	}
}

void EchoCanceller::pushFarEnd(const qint16 *pcm, int nsamples)
{
	if (!m_echo || !pcm || nsamples <= 0)
		return;

	QMutexLocker locker(&m_farMutex);

	// Reclaim the space already consumed rather than growing without bound;
	// doing it only once the head has run past half the buffer keeps this
	// from turning into a memmove on every call.
	if (m_farHead > 0 && m_farHead >= m_farFifo.size() / 2)
	{
		m_farFifo.remove(0, m_farHead);
		m_farHead = 0;
	}

	int oldSize = m_farFifo.size();
	m_farFifo.resize(oldSize + nsamples);
	memcpy(m_farFifo.data() + oldSize, pcm, nsamples * sizeof(qint16));

	int pending = m_farFifo.size() - m_farHead;
	if (pending > m_farCapacity)
	{
		m_farHead += pending - m_farCapacity;
		if (m_farHead >= m_farFifo.size())
		{
			m_farFifo.clear();
			m_farHead = 0;
		}
	}
}

bool EchoCanceller::popFarEnd(qint16 *dst, int nsamples)
{
	QMutexLocker locker(&m_farMutex);

	int pending = m_farFifo.size() - m_farHead;
	if (pending <= 0)
		return false;

	int n = pending < nsamples ? pending : nsamples;
	memcpy(dst, m_farFifo.constData() + m_farHead, n * sizeof(qint16));
	m_farHead += n;

	if (n < nsamples)
		memset(dst + n, 0, (nsamples - n) * sizeof(qint16));

	return true;
}

void EchoCanceller::processNearEnd(qint16 *pcm, int nsamples)
{
	if (!m_echo || !pcm || nsamples < m_frameSize)
		return;

	QMutexLocker locker(&m_procMutex);

	SpeexEchoState *echo = reinterpret_cast<SpeexEchoState *>(m_echo);
	SpeexPreprocessState *pre = reinterpret_cast<SpeexPreprocessState *>(m_preprocess);

	double nearEnergy = 0.0;
	double outEnergy = 0.0;

	int blocks = nsamples / m_frameSize;
	for (int b = 0; b < blocks; b++)
	{
		// Not called "near" - windef.h still defines that as an empty macro
		// on the mingw-w64 builds.
		qint16 *nearBlock = pcm + b * m_frameSize;

		for (int i = 0; i < m_frameSize; i++)
			nearEnergy += double(nearBlock[i]) * double(nearBlock[i]);

		if (!popFarEnd(m_farBlock.data(), m_frameSize))
		{
			// Nothing is being played, so there is no echo to remove. Still
			// run the frame through, otherwise the filter's idea of the far
			// end signal silently runs ahead of the microphone.
			memset(m_farBlock.data(), 0, m_frameSize * sizeof(qint16));
		}

		// speex_echo_cancellation() is happy to work in place on the near end
		// buffer, which is what lets this sit directly in the capture path
		// without another copy.
		speex_echo_cancellation(echo, nearBlock, m_farBlock.constData(), nearBlock);

		if (pre)
			speex_preprocess_run(pre, nearBlock);

		for (int i = 0; i < m_frameSize; i++)
			outEnergy += double(nearBlock[i]) * double(nearBlock[i]);
	}

	QMutexLocker statLocker(&m_statMutex);
	// Exponential forgetting, so erle() reflects the last few seconds rather
	// than the whole call.
	m_nearEnergy = m_nearEnergy * 0.95 + nearEnergy * 0.05;
	m_outEnergy = m_outEnergy * 0.95 + outEnergy * 0.05;
}

double EchoCanceller::erle()
{
	QMutexLocker locker(&m_statMutex);

	if (m_nearEnergy <= 0.0 || m_outEnergy <= 0.0)
		return 0.0;

	return 10.0 * log10(m_nearEnergy / m_outEnergy);
}
