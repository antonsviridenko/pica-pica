/*
	(c) Copyright  2012 - 2021 Anton Sviridenko
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
#ifndef AUDIOVIDEOCALLCONTROLLER_H
#define AUDIOVIDEOCALLCONTROLLER_H

#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include "callwindow.h"
#include "audiodevice.h"
#include "capturegain.h"
#include "videodevice.h"
#include "toneplayer/toneplayer.h"

class AudioVideoCallController : public QObject
{
	Q_OBJECT
public:
	explicit AudioVideoCallController(QObject *parent = nullptr);
	~AudioVideoCallController();
	// is call active (i.e. are voice/video packets exchanged now)
	bool const isActive() {return is_active;}
	void processCall();
public slots:
	void start_call(QByteArray peer_id);
	void call_from(QByteArray peer_id);
	void call_failed(QByteArray peer_id, QString reason);
	void call_accepted(QByteArray peer_id);
	void call_rejected(QByteArray peer_id);
	void call_hungup(QByteArray peer_id);
signals:
private:
	CallWindow *callwindow;
	AudioDevice *ringdevice;
	AudioDevice *microphone;
	AudioDevice *output;
	// Local camera capture+encode.
	VideoDevice *cam;
	// Decoding of the video stream received from the peer.
	VideoDevice *remotevideo;
	bool is_active;
	QByteArray m_peer_id;

	// Earpiece tones (ringback, busy, special information/unreachable),
	// played through the "audio.playback_device" setting.
	TonePlayer *toneplayer;
	QThread toneplayer_thread;

	// Incoming call bell, played through the "audio.ring_device" setting.
	TonePlayer *ringtoneplayer;
	QThread ringtoneplayer_thread;

	// Mic capture+encode and remote-audio decode+playback, each blocking
	// its own thread for the duration of the call (see AudioDevice).
	QThread microphone_thread;
	QThread output_thread;

	// Camera capture+encode and remote-video decode, same arrangement as
	// the audio devices above (see VideoDevice).
	QThread cam_thread;
	QThread remotevideo_thread;

	// Acoustic echo cancellation for the current call, shared by microphone
	// and output. Null when it is switched off in the settings, when
	// speexdsp could not start one, or between calls.
	EchoCancellerPtr m_echoCanceller;

	// The capture device's mixer control, held for the duration of a call so
	// a clipping microphone can be turned down. Null when the setting is off,
	// when the platform has no reachable control, or between calls.
	//
	// m_savedCaptureGain is where the control was found at the start of the
	// call, put back in stopAudioPipeline(): this is the user's own setting
	// and we are only borrowing it.
	CaptureGainControl *m_captureGain;
	double m_savedCaptureGain;
	bool m_haveSavedCaptureGain;

	// This is a calibration that runs at the start of a call and then stops -
	// NOT a gain control that rides the level for the call's duration. The
	// difference matters: an adaptive echo canceller can only track a
	// stationary echo path, and moving the microphone gain moves that path.
	// See the AGC comment in EchoCanceller's constructor, which says the same
	// thing about the reason speexdsp's own AGC is left switched off.
	//
	// So every adjustment resets the canceller, and once m_gainCalibrating is
	// false nothing touches the control again until the call ends.
	bool m_gainCalibrating;
	int m_captureGainSteps;

	// dB reading when calibration opened, and whether the platform would give
	// us one. Bounding the total cut in decibels rather than as a fraction of
	// the control's travel is the whole point: a fraction means different
	// things on a hardware mixer (linear in dB) and on a sound server or
	// WASAPI endpoint (roughly cubic), which is how an earlier version of this
	// managed to wind a microphone down to silence.
	double m_gainStartDb;
	bool m_haveGainDb;

	// Consecutive adjustments that moved the control but did not reduce the
	// clipping. Means the stage actually overloading is one we cannot reach -
	// an analog mic boost ahead of the converter, or Windows' separate
	// "Microphone Boost" - and that turning our control down further will
	// silence the user without ever fixing it.
	int m_gainIneffectiveSteps;
	double m_lastPinnedPercent;

	// Reports to ignore after an adjustment, while audio captured at the old
	// setting drains out of the pipeline.
	int m_gainSettleReports;

	static const int kMaxCaptureGainSteps = 6;

	// Bound used when the control will not report decibels at all, where we
	// are working blind and a wrong guess is what silences a microphone.
	static const int kMaxBlindGainSteps = 2;
	static const int kMaxGainIneffectiveSteps = 2;
	static const int kGainSettleReports = 1;

	// How long the calibration window stays open, in milliseconds of call
	// time. Long enough that someone who says nothing for the first few
	// seconds still gets calibrated, short enough to be over before the
	// conversation proper.
	static const int kGainCalibrationMs = 20000;

	// Hard ceiling on the total cut, in dB below where the user had it. Past
	// this the problem is not one we are going to fix by turning a knob.
	static constexpr double kMaxGainReductionDb = 24.0;

	// An adjustment that achieves less than this much is not doing anything.
	static constexpr double kMinEffectiveStepDb = 0.5;

	// Sequence number / capture-clock timestamp stamped onto outgoing
	// 0x76 audio packets (see PICA_PROTO_CALL_PACKET_HDRSIZE).
	quint16 m_audioSeq;
	QElapsedTimer m_callClock;

	// Outgoing 0x77 video packets. The counter occupies the low 15 bits of
	// the wire sequence number, the top bit being the last fragment marker.
	// All fragments of one encoded frame share m_videoFrameTimestamp, which
	// is re-read from m_callClock only when a new frame starts - tracked by
	// m_videoFrameStarted, set false again after emitting a last fragment.
	quint16 m_videoSeq;
	quint32 m_videoFrameTimestamp;
	bool m_videoFrameStarted;

	// Reassembly of incoming 0x77 fragments into whole encoded frames.
	VideoFrameAssembler m_videoAssembler;

	// Take over the capture device's mixer gain for the duration of a call,
	// and hand it back. Both are no-ops when "audio.auto_capture_gain" is off
	// or the platform has no control we can reach.
	void startCaptureGainControl(const QString &captureDevice);
	void stopCaptureGainControl();

	// Ends the calibration window, logging why. Idempotent.
	void closeGainCalibration(const QString &why);

	void playEarpieceTone(void (TonePlayer::*tone)());
	void playRingTone();
	void stopTones();

	void startAudioPipeline();
	void stopAudioPipeline();

	void startVideoPipeline();
	void stopVideoPipeline();

private slots:
	// button handlers
	void initiate_call();
	void accept_call();
	void end_call();
	void callwindow_closed(CallWindow *sender_window);

	void send_audio_packet(QByteArray data);
	void incoming_audio_params(QByteArray peer_id, QString codec, quint16 sample_rate);
	void capture_clipping(double pinnedPercent, double peakDb);
	void incoming_audio_packet(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data);

	void video_capture_started(QString codec, int width, int height);
	void send_video_packet(QByteArray data, bool is_last_fragment);
	void incoming_video_params(QByteArray peer_id, QString codec, quint16 width, quint16 height);
	void incoming_video_packet(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data);

	void media_transport_changed(QByteArray peer_id, bool direct_udp, QString ciphersuitename, quint32 max_payload);
#ifdef HAVE_VAAPI
	void video_rendering_failed();
#endif

};

#endif // AUDIOVIDEOCALLCONTROLLER_H
