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
#include "callsettings.h"
#include "settings.h"
#include "globals.h"

const CallCodecInfo kCallVideoCodecs[] =
{
	{ "h264", "H.264"      },
	{ "hevc", "H.265/HEVC" },
	{ "vp9",  "VP9"        }
};

const int kCallVideoCodecCount = sizeof(kCallVideoCodecs) / sizeof(kCallVideoCodecs[0]);

const CallCodecInfo kCallAudioCodecs[] =
{
	{ "opus",       "Opus"        },
	{ "adpcm_g722", "G.722"       },
	{ "pcm_alaw",   "G.711 A-law" }
};

const int kCallAudioCodecCount = sizeof(kCallAudioCodecs) / sizeof(kCallAudioCodecs[0]);

int callAudioSampleRate(const QString &codec)
{
	// G.722 is a 16kHz codec by definition - it subband-codes a 7kHz audio
	// bandwidth and there is no other rate to run it at.
	if (codec == QLatin1String("adpcm_g722"))
		return 16000;

	// A-law is the telephone network's codec and 8kHz is what it is for.
	// Running it at anything else would only spend twice the bandwidth on a
	// codec chosen for costing as little as possible.
	if (codec == QLatin1String("pcm_alaw"))
		return 8000;

	// Opus, and anything unrecognised: 16kHz rather than 48kHz because it is
	// what the echo canceller wants. AEC3 does its filtering in the 0-8kHz
	// band whatever it is handed; at 48kHz it splits the signal into four
	// bands, cancels in the lowest and gates the rest, which is more work for
	// a result that is no better on speech.
	//
	// Costs nothing worth having for speech: Opus codes this as wideband, an
	// 8kHz audio bandwidth, which is well past telephone quality and past
	// where voice intelligibility stops improving.
	//
	// Note that this is the capture direction only. Playback follows whatever
	// the peer announced in its own 0x74 message (see
	// AudioVideoCallController::incoming_audio_params()), so a call with a
	// peer that picked a different codec or rate keeps working.
	return 16000;
}

bool callAudioBitrateAdjustable(const QString &codec)
{
	// Opus is the only one of the three with a rate control worth the name;
	// the other two are fixed bits per sample.
	return codec == QLatin1String("opus") || codec.isEmpty();
}

int callAudioFixedBitrateKbps(const QString &codec)
{
	// G.722 is 4 bits per sample at 16kHz, A-law 8 bits per sample at 8kHz -
	// 64 kbit/s either way, which is no coincidence.
	if (codec == QLatin1String("adpcm_g722"))
		return 16000 * 4 / 1000;

	if (codec == QLatin1String("pcm_alaw"))
		return 8000 * 8 / 1000;

	return kDefaultAudioBitrateKbps;
}

CallSettings CallSettings::load()
{
	Settings st(config_dbname);
	CallSettings cs;

	cs.videoCodec = st.loadValue("call.video_codec", QLatin1String(kCallVideoCodecs[0].name)).toString();
	cs.audioCodec = st.loadValue("call.audio_codec", QLatin1String(kCallAudioCodecs[0].name)).toString();

	cs.videoBitrateKbps = st.loadValue("call.video_bitrate_kbps", kDefaultVideoBitrateKbps).toInt();
	cs.audioBitrateKbps = st.loadValue("call.audio_bitrate_kbps", kDefaultAudioBitrateKbps).toInt();

	cs.captureWidth = st.loadValue("video.capture_width", kDefaultCaptureWidth).toInt();
	cs.captureHeight = st.loadValue("video.capture_height", kDefaultCaptureHeight).toInt();
	cs.captureFrameRate = st.loadValue("video.capture_framerate", kDefaultCaptureFrameRate).toInt();

	// A stored value that makes no sense - a hand-edited database, or a
	// setting written by a build that offered something this one does not -
	// would otherwise be passed to the encoder and fail the call rather than
	// the setting.
	if (cs.videoBitrateKbps <= 0)
		cs.videoBitrateKbps = kDefaultVideoBitrateKbps;

	if (cs.audioBitrateKbps <= 0)
		cs.audioBitrateKbps = kDefaultAudioBitrateKbps;

	if (cs.captureWidth <= 0 || cs.captureHeight <= 0)
	{
		cs.captureWidth = kDefaultCaptureWidth;
		cs.captureHeight = kDefaultCaptureHeight;
	}

	if (cs.captureFrameRate <= 0)
		cs.captureFrameRate = kDefaultCaptureFrameRate;

	return cs;
}
