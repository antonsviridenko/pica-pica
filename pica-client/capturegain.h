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
#ifndef CAPTUREGAIN_H
#define CAPTUREGAIN_H

#include <QString>

// The capture device's own gain control - the mixer slider, not a multiplier
// applied to samples we already have.
//
// The distinction is the entire point of this class. Clipping happens in the
// converter, before any buffer reaches us; by then the peaks are flat and the
// harmonics they generated are in the signal for good. Scaling that buffer
// down produces a quieter clipped buffer and nothing else - in particular it
// does not help the echo canceller, whose adaptive filter is linear and
// therefore scale invariant. What defeats the filter is the non-linearity, and
// the only place to prevent one is ahead of the ADC. That is this control.
//
// Gain is expressed as a 0.0 to 1.0 fraction of the control's travel, which is
// what WASAPI and CoreAudio use natively and what ALSA and PulseAudio's ranges
// map onto cleanly. The mapping from that fraction to decibels is the
// platform's business and is not the same on all of them, so callers should
// treat a step as "somewhat quieter" and close the loop by measuring again
// rather than doing arithmetic in dB.
//
// Not thread safe, and deliberately so: AudioVideoCallController owns one of
// these and touches it only from the GUI thread, driven by a queued signal
// from the capture thread. Some of the implementations below need a message
// loop or a COM apartment and have no business being called from an audio
// callback.
class CaptureGainControl
{
public:
	virtual ~CaptureGainControl();

	// Current position of the control, 0.0 to 1.0. False if it could not be
	// read, in which case *out is untouched.
	virtual bool gain(double *out) = 0;

	// Moves the control. Values outside 0.0 to 1.0 are clamped.
	virtual bool setGain(double value) = 0;

	// What was actually found - the mixer element or endpoint name - for the
	// log line that says the gain is being taken over.
	virtual QString description() const = 0;

	// driver is a name from AudioDevice::PlatformDriverName(), device the
	// capture device string that goes with it. Returns null when this build
	// or this platform has no way to reach the control, which is not an
	// error: the caller carries on without automatic gain and the clipping
	// warning still gets shown.
	static CaptureGainControl *create(const QString &driver, const QString &device);
};

#endif // CAPTUREGAIN_H
