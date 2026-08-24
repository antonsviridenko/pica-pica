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
#ifndef AUDIORING_H
#define AUDIORING_H

#include <QVector>
#include <QAtomicInt>
#include <cstring>

// Lock-free single-producer / single-consumer ring of 16 bit samples.
//
// The native audio backends (WASAPI, CoreAudio) deliver and ask for audio
// from a callback running on a real time thread owned by the OS. That thread
// must never block, so it cannot take the same mutex as the AudioDevice
// capture/playback loop it is exchanging samples with. Only two threads ever
// touch one ring - the callback on one end, the AudioDevice loop on the
// other - which is exactly the case this structure is safe for.
//
// Overflow (producer faster than consumer) and underflow (the reverse) are
// both normal in an audio path that has just started or has been starved, so
// they are counted rather than treated as errors: the caller decides whether
// to pad with silence or drop.
class AudioRing
{
public:
	// capacity is in samples and is rounded up to a power of two, so the
	// index arithmetic below is a mask rather than a modulo.
	explicit AudioRing(int capacity = 48000)
		: m_read(0), m_write(0), m_overruns(0), m_underruns(0)
	{
		int cap = 1;
		while (cap < capacity)
			cap <<= 1;
		m_buf.resize(cap);
		m_mask = cap - 1;
	}

	void clear()
	{
		m_read.storeRelease(0);
		m_write.storeRelease(0);
		m_overruns.storeRelaxed(0);
		m_underruns.storeRelaxed(0);
	}

	int capacity() const { return m_buf.size(); }

	// Samples waiting to be read. Safe to call from either side; the answer
	// is a lower bound for the consumer and an upper bound for the producer,
	// which is the direction each of them needs it to be conservative in.
	int available() const
	{
		return m_write.loadAcquire() - m_read.loadAcquire();
	}

	int freeSpace() const { return m_buf.size() - available(); }

	int overruns() const { return m_overruns.loadRelaxed(); }
	int underruns() const { return m_underruns.loadRelaxed(); }

	// Producer side. Writes as much as fits and drops the rest, counting one
	// overrun if anything had to be dropped. Returns the number written.
	int write(const qint16 *src, int nsamples)
	{
		const int w = m_write.loadRelaxed();
		int space = m_buf.size() - (w - m_read.loadAcquire());
		int n = nsamples < space ? nsamples : space;

		if (n < nsamples)
			m_overruns.fetchAndAddRelaxed(1);

		for (int i = 0; i < n; )
		{
			int idx = (w + i) & m_mask;
			int chunk = m_buf.size() - idx;
			if (chunk > n - i)
				chunk = n - i;
			memcpy(m_buf.data() + idx, src + i, chunk * sizeof(qint16));
			i += chunk;
		}

		m_write.storeRelease(w + n);
		return n;
	}

	// Producer side, for a playback ring the render callback drains: fill
	// with nsamples of silence, used to keep the timeline moving when there
	// is nothing to play.
	int writeSilence(int nsamples)
	{
		const int w = m_write.loadRelaxed();
		int space = m_buf.size() - (w - m_read.loadAcquire());
		int n = nsamples < space ? nsamples : space;

		for (int i = 0; i < n; )
		{
			int idx = (w + i) & m_mask;
			int chunk = m_buf.size() - idx;
			if (chunk > n - i)
				chunk = n - i;
			memset(m_buf.data() + idx, 0, chunk * sizeof(qint16));
			i += chunk;
		}

		m_write.storeRelease(w + n);
		return n;
	}

	// Consumer side. Returns the number of samples actually read; the caller
	// gets no padding, see readOrSilence() for that.
	int read(qint16 *dst, int nsamples)
	{
		const int r = m_read.loadRelaxed();
		int avail = m_write.loadAcquire() - r;
		int n = nsamples < avail ? nsamples : avail;

		if (n < nsamples)
			m_underruns.fetchAndAddRelaxed(1);

		for (int i = 0; i < n; )
		{
			int idx = (r + i) & m_mask;
			int chunk = m_buf.size() - idx;
			if (chunk > n - i)
				chunk = n - i;
			memcpy(dst + i, m_buf.constData() + idx, chunk * sizeof(qint16));
			i += chunk;
		}

		m_read.storeRelease(r + n);
		return n;
	}

	// Consumer side. Always fills the whole buffer, padding the tail with
	// silence if the ring ran dry - what an output callback needs, since it
	// has to hand back a full buffer no matter what.
	void readOrSilence(qint16 *dst, int nsamples)
	{
		int got = read(dst, nsamples);
		if (got < nsamples)
			memset(dst + got, 0, (nsamples - got) * sizeof(qint16));
	}

	// Consumer side. Throws away everything older than the newest nsamples,
	// used to bound the latency of a ring whose producer has outrun it.
	void trimTo(int nsamples)
	{
		int avail = available();
		if (avail > nsamples)
			m_read.fetchAndAddRelease(avail - nsamples);
	}

private:
	QVector<qint16> m_buf;
	int m_mask;

	// Free running counts of samples read and written. They only ever grow,
	// so the difference is the fill level; wrapping at 2^31 samples is a bit
	// over 12 hours of a 48kHz call and would cost one bad read at worst.
	QAtomicInt m_read;
	QAtomicInt m_write;

	QAtomicInt m_overruns;
	QAtomicInt m_underruns;
};

#endif // AUDIORING_H
