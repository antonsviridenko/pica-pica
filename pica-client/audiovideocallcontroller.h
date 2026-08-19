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
