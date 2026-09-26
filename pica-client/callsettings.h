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
#ifndef CALLSETTINGS_H
#define CALLSETTINGS_H

#include <QString>

// The parameters an audio/video call is started with: what the settings
// dialog's "Call Settings" and "Video Devices" tabs write, and what
// AudioVideoCallController reads when a call begins. Both sides go through
// this one place so they cannot drift apart, and so that "never configured"
// means the same thing to each of them.
//
// Codec names are FFmpeg names throughout. That is also how the 0x74 and 0x75
// protocol messages name them (see doc/proto-doc-latest.txt), so what is
// stored here goes both onto the wire and into
// avcodec_find_encoder_by_name() with no translation table in between.

// One entry of the codec lists the settings dialog offers.
struct CallCodecInfo
{
	// The FFmpeg name, which is what gets stored and what goes on the wire.
	const char *name;
	// What the settings dialog shows for it.
	const char *displayName;
};

// Video codecs offered by the "Video Codec" setting, in the order they are
// listed. Every one of them is a codec the 0x75 message can name and that the
// receiving side hands to avcodec_find_decoder_by_name(), so no peer needs to
// know in advance which one we picked.
extern const CallCodecInfo kCallVideoCodecs[];
extern const int kCallVideoCodecCount;

// Audio codecs offered by the "Audio Codec" setting - the three the protocol
// names for the 0x74 message.
extern const CallCodecInfo kCallAudioCodecs[];
extern const int kCallAudioCodecCount;

// What a call uses when nothing has ever been configured: the values that were
// compiled in before any of this became a setting, so an install that has
// never opened the settings dialog behaves exactly as it did.
const int kDefaultVideoBitrateKbps = 600;
const int kDefaultAudioBitrateKbps = 32;
const int kDefaultCaptureWidth = 640;
const int kDefaultCaptureHeight = 480;
const int kDefaultCaptureFrameRate = 15;

struct CallSettings
{
	// FFmpeg codec names, e.g. "h264" and "opus".
	QString videoCodec;
	QString audioCodec;

	// As the user enters them, in kbit/s.
	int videoBitrateKbps;
	int audioBitrateKbps;

	// What the camera is asked for. Not necessarily what it delivers - see
	// VideoDevice::Capture(), which loosens these a step at a time rather than
	// failing when a camera will not honour them.
	int captureWidth;
	int captureHeight;
	int captureFrameRate;

	// Reads the stored settings, filling in the defaults above for anything
	// that was never set. Must be called from a thread that may touch the
	// settings database - not from a capture or playback loop, see
	// AudioDevice::SetLinuxDriverName() for why.
	static CallSettings load();
};

// The rate a given audio codec is captured and encoded at, and announced at in
// the 0x74 message.
//
// It is a property of the codec rather than a setting: G.722 is defined at
// 16kHz and nowhere else, and A-law is a telephony codec whose whole point is
// 8kHz. Opus would take any of them and uses 16kHz for the reason spelled out
// in callsettings.cpp.
int callAudioSampleRate(const QString &codec);

// Whether the codec's bitrate is the caller's to choose. Opus is; G.722 and
// A-law each pack a fixed number of bits per sample, so their bitrate follows
// from the sample rate and nothing can be asked of the encoder.
bool callAudioBitrateAdjustable(const QString &codec);

// The bitrate a fixed rate codec ends up at, in kbit/s, for showing in the
// settings dialog. Meaningless for a codec callAudioBitrateAdjustable() is
// true for, which returns the configured value instead.
int callAudioFixedBitrateKbps(const QString &codec);

#endif // CALLSETTINGS_H
