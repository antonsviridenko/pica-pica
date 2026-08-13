/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
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
#include "audiovideocallcontroller.h"
#include "globals.h"
#include "skynet.h"
#include "settings.h"
#include <QByteArray>
#include <QDebug>
#include <QMetaObject>

// Capture/encode resolution announced to the peer in the 0x75 message. Fixed
// for now - a resolution setting is a planned follow-up, see VideoDevice for
// the rest of the encoding parameters.
static const quint16 kVideoWidth = 640;
static const quint16 kVideoHeight = 480;


AudioVideoCallController::AudioVideoCallController(QObject *parent)
 : QObject(parent), callwindow(0), ringdevice(0), is_active(false), m_audioSeq(0),
   m_videoSeq(0), m_videoFrameTimestamp(0), m_videoFrameStarted(false)
{
	connect(skynet, SIGNAL(IncomingCall(QByteArray)), this, SLOT(call_from(QByteArray)));
	connect(skynet, SIGNAL(CallFailed(QByteArray,QString)), this, SLOT(call_failed(QByteArray,QString)));
	connect(skynet, SIGNAL(CallAccepted(QByteArray)), this, SLOT(call_accepted(QByteArray)));
	connect(skynet, SIGNAL(CallRejected(QByteArray)), this, SLOT(call_rejected(QByteArray)));
	connect(skynet, SIGNAL(CallHungup(QByteArray)), this, SLOT(call_hungup(QByteArray)));

	connect(skynet, SIGNAL(IncomingAudioParams(QByteArray,QString,quint16)), this, SLOT(incoming_audio_params(QByteArray,QString,quint16)));
	connect(skynet, SIGNAL(IncomingAudioPacket(QByteArray,quint16,quint32,QByteArray)), this, SLOT(incoming_audio_packet(QByteArray,quint16,quint32,QByteArray)));

	connect(skynet, SIGNAL(IncomingVideoParams(QByteArray,QString,quint16,quint16)), this, SLOT(incoming_video_params(QByteArray,QString,quint16,quint16)));
	connect(skynet, SIGNAL(IncomingVideoPacket(QByteArray,quint16,quint32,QByteArray)), this, SLOT(incoming_video_packet(QByteArray,quint16,quint32,QByteArray)));

	// Earpiece tones (ringback/busy/unreachable) and the incoming call bell
	// are played by two TonePlayer instances, each living on its own
	// QThread since TonePlayer::play() blocks the calling thread for the
	// duration of the tone sequence.
	toneplayer = new TonePlayer();
	toneplayer->moveToThread(&toneplayer_thread);
	toneplayer_thread.start();

	ringtoneplayer = new TonePlayer();
	ringtoneplayer->moveToThread(&ringtoneplayer_thread);
	ringtoneplayer_thread.start();

	// Mic capture+encode and remote-audio decode+playback - same
	// one-object-per-thread pattern as the tone players above, since
	// AudioDevice::Capture()/Play() block their thread for the call's
	// duration too.
	microphone = new AudioDevice();
	microphone->moveToThread(&microphone_thread);
	connect(microphone, SIGNAL(packetReady(QByteArray)), this, SLOT(send_audio_packet(QByteArray)));
	microphone_thread.start();

	output = new AudioDevice();
	output->moveToThread(&output_thread);
	output_thread.start();

	// Camera capture+encode and remote video decode - same arrangement as
	// the audio devices above.
	cam = new VideoDevice();
	cam->moveToThread(&cam_thread);
	connect(cam, SIGNAL(captureStarted(QString,int,int)), this, SLOT(video_capture_started(QString,int,int)));
	connect(cam, SIGNAL(packetReady(QByteArray,bool)), this, SLOT(send_video_packet(QByteArray,bool)));
	cam_thread.start();

	remotevideo = new VideoDevice();
	remotevideo->moveToThread(&remotevideo_thread);
	remotevideo_thread.start();
}

AudioVideoCallController::~AudioVideoCallController()
{
	stopTones();
	stopAudioPipeline();
	stopVideoPipeline();

	toneplayer_thread.quit();
	toneplayer_thread.wait();
	delete toneplayer;

	ringtoneplayer_thread.quit();
	ringtoneplayer_thread.wait();
	delete ringtoneplayer;

	microphone_thread.quit();
	microphone_thread.wait();
	delete microphone;

	output_thread.quit();
	output_thread.wait();
	delete output;

	cam_thread.quit();
	cam_thread.wait();
	delete cam;

	remotevideo_thread.quit();
	remotevideo_thread.wait();
	delete remotevideo;
}

void AudioVideoCallController::playEarpieceTone(void (TonePlayer::*tone)())
{
	Settings st(config_dbname);
	QString dev = st.loadValue("audio.playback_device", "default").toString();

	toneplayer->stop();
	QMetaObject::invokeMethod(toneplayer, "setDeviceName", Qt::QueuedConnection, Q_ARG(QString, dev));
	if (tone == &TonePlayer::playCallInProgress)
		QMetaObject::invokeMethod(toneplayer, "playCallInProgress", Qt::QueuedConnection);
	else if (tone == &TonePlayer::playUnreachable)
		QMetaObject::invokeMethod(toneplayer, "playUnreachable", Qt::QueuedConnection);
	else if (tone == &TonePlayer::playBusy)
		QMetaObject::invokeMethod(toneplayer, "playBusy", Qt::QueuedConnection);
}

void AudioVideoCallController::playRingTone()
{
	Settings st(config_dbname);
	QString dev = st.loadValue("audio.ring_device", "default").toString();

	ringtoneplayer->stop();
	QMetaObject::invokeMethod(ringtoneplayer, "setDeviceName", Qt::QueuedConnection, Q_ARG(QString, dev));
	QMetaObject::invokeMethod(ringtoneplayer, "playClassicRingtone", Qt::QueuedConnection);
}

void AudioVideoCallController::stopTones()
{
	toneplayer->stop();
	ringtoneplayer->stop();
}

void AudioVideoCallController::startAudioPipeline()
{
	Settings st(config_dbname);
	QString capDev = st.loadValue("audio.capture_device", "default").toString();

	m_audioSeq = 0;
	m_callClock.start();

	// Declare our outgoing codec to the peer, then start capturing and
	// encoding right away - the peer's decoder is only ready once it has
	// processed this 0x74 message, but since the call is carried over TCP,
	// packets sent immediately after are guaranteed to arrive after it.
	skynet->SendAudioParams(m_peer_id, QStringLiteral("opus"), 48000);

	QMetaObject::invokeMethod(microphone, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, capDev), Q_ARG(QString, QStringLiteral("opus")), Q_ARG(int, 48000));
	QMetaObject::invokeMethod(microphone, "Capture", Qt::QueuedConnection);

	// output is configured lazily, once the peer's own 0x74 (see
	// incoming_audio_params()) tells us what codec/rate it is sending.
}

void AudioVideoCallController::stopAudioPipeline()
{
	// Close() only touches a QAtomicInt and wakes a QWaitCondition, both
	// safe to reach from any thread - it must be called directly rather
	// than via invokeMethod()/QueuedConnection: microphone/output's own
	// thread is blocked inside Capture()/Play() for the call's duration,
	// so its event loop cannot deliver a queued call back to itself until
	// that blocking call returns - which is exactly what Close() is
	// supposed to make happen. Routing it through the event queue would
	// deadlock (the thread would never quit, and the app would hang on
	// exit after a call).
	microphone->Close();
	output->Close();
}

void AudioVideoCallController::startVideoPipeline()
{
	Settings st(config_dbname);
	QString camDev = st.loadValue("video.capture_device", QString()).toString();

	// Unlike the audio devices, which fall back to the platform's "default"
	// device, there is no such name for cameras - so with nothing configured
	// yet (the setting is only written once the settings dialog has been
	// visited) pick the first camera found.
	if (camDev.isEmpty())
	{
		VideoDevice enumerator;
		QList<MediaDeviceInfo> cams = enumerator.Enumerate(CAPTURE);

		if (!cams.isEmpty())
			camDev = cams.first().device;
	}

	// No camera at all - the call simply carries no outgoing video. Incoming
	// video is unaffected, it only depends on what the peer sends.
	if (camDev.isEmpty())
		return;

	m_videoSeq = 0;
	m_videoFrameTimestamp = 0;
	m_videoFrameStarted = false;

	bool preferCompressed = st.loadValue("video.prefer_compressed", 0).toBool();

	// The peer is told what we are sending only once the camera is open and
	// the format is settled - with a compressed camera stream being forwarded
	// as-is, the codec is the camera's choice, not ours (see
	// video_capture_started()).
	QMetaObject::invokeMethod(cam, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, camDev),
	                           Q_ARG(int, kVideoWidth), Q_ARG(int, kVideoHeight),
	                           Q_ARG(bool, preferCompressed));
	QMetaObject::invokeMethod(cam, "Capture", Qt::QueuedConnection);

	// remotevideo is configured lazily, once the peer's own 0x75 (see
	// incoming_video_params()) tells us what it is sending.
}

void AudioVideoCallController::stopVideoPipeline()
{
	// Direct calls, for the same reason stopAudioPipeline() uses them.
	cam->Close();
	remotevideo->Close();

	m_videoAssembler.reset();
}

void AudioVideoCallController::call_failed(QByteArray peer_id, QString reason)
{
	qDebug() << "Call failed: " << reason << "\n";

	if (peer_id != m_peer_id)
		return;

	is_active = false;
	/* play unreachable */
	playEarpieceTone(&TonePlayer::playUnreachable);
}

void AudioVideoCallController::call_accepted(QByteArray peer_id)
{
	/* SkyNet lives in its own thread, so this signal is queued: by the time
	   it is delivered the local side may have already hung up and closed the
	   call window, or moved on to a different peer entirely. Either way the
	   call it refers to is gone - do not bring the audio pipeline up for it. */
	if (peer_id != m_peer_id || !callwindow)
		return;

	stopTones();
	is_active = true;
	callwindow->call_started();
	startAudioPipeline();
	startVideoPipeline();
}

void AudioVideoCallController::call_rejected(QByteArray peer_id)
{
	if (peer_id != m_peer_id)
		return;

	/* play busy tone */
	playEarpieceTone(&TonePlayer::playBusy);
}

void AudioVideoCallController::call_hungup(QByteArray peer_id)
{
	if (peer_id != m_peer_id)
		return;

	stopAudioPipeline();
	stopVideoPipeline();

	/* queued from the SkyNet thread as well - the window may already be closed */
	if (callwindow)
		callwindow->call_ended();

	/* play busy tone */
	playEarpieceTone(&TonePlayer::playBusy);
	is_active = false;
}

void AudioVideoCallController::start_call(QByteArray peer_id)
{
	if (callwindow)
	{
		callwindow->show();


		/* call is already in progress */
		if (is_active)
			return;
		/* re-create callwindow for a new call */
		callwindow->close();
	}

	m_peer_id = peer_id;
	callwindow = new CallWindow(peer_id, false);
	connect(callwindow, SIGNAL(start_call_pressed()), this, SLOT(initiate_call()));
	connect(callwindow, SIGNAL(hang_call_pressed()), this, SLOT(end_call()));
	connect(callwindow, SIGNAL(callwindow_closed(CallWindow*)), this, SLOT(callwindow_closed(CallWindow*)));
	/* decoded frames arrive from remotevideo's thread, so this is a queued
	   connection; it dies with the window, which is deleted on close */
	connect(remotevideo, SIGNAL(frameReady(QImage)), callwindow, SLOT(showRemoteFrame(QImage)));
	callwindow->show();
}

void AudioVideoCallController::call_from(QByteArray peer_id)
{
	if (callwindow)
	{
		/* HANDLE BUSY CASE!!! */
		/* call is already in progress */
		return;
	}

	m_peer_id = peer_id;
	callwindow = new CallWindow(peer_id, true);
	connect(callwindow, SIGNAL(accept_call_pressed()), this, SLOT(accept_call()));
	connect(callwindow, SIGNAL(hang_call_pressed()), this, SLOT(end_call()));
	connect(callwindow, SIGNAL(callwindow_closed(CallWindow*)), this, SLOT(callwindow_closed(CallWindow*)));
	/* see the same connection in start_call() */
	connect(remotevideo, SIGNAL(frameReady(QImage)), callwindow, SLOT(showRemoteFrame(QImage)));
	callwindow->show();

	/* play ring tone */
	playRingTone();
}

void AudioVideoCallController::initiate_call()
{
	skynet->StartCall(m_peer_id);
	/* play ringing tone */
	playEarpieceTone(&TonePlayer::playCallInProgress);
}

void AudioVideoCallController::accept_call()
{
	stopTones();
	skynet->AcceptCall(m_peer_id);
	is_active = true;
	callwindow->call_started();
	startAudioPipeline();
	startVideoPipeline();
}

void AudioVideoCallController::end_call()
{
	stopTones();
	stopAudioPipeline();
	stopVideoPipeline();

	if (is_active)
		skynet->HangupCall(m_peer_id);
	else
		skynet->RejectCall(m_peer_id);

	is_active = false;
	callwindow->close();
}

void AudioVideoCallController::callwindow_closed(CallWindow *sender_window)
{
	callwindow = 0;
	is_active = false;
}

void AudioVideoCallController::send_audio_packet(QByteArray data)
{
	if (!is_active)
		return;

	quint16 seq = m_audioSeq++;
	quint32 timestamp = (quint32)m_callClock.elapsed();

	skynet->SendAudioPacket(m_peer_id, seq, timestamp, data);
}

void AudioVideoCallController::incoming_audio_params(QByteArray peer_id, QString codec, quint16 sample_rate)
{
	if (!is_active || peer_id != m_peer_id)
		return;

	Settings st(config_dbname);
	QString playDev = st.loadValue("audio.playback_device", "default").toString();

	QMetaObject::invokeMethod(output, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, playDev), Q_ARG(QString, codec), Q_ARG(int, (int)sample_rate));
	QMetaObject::invokeMethod(output, "Play", Qt::QueuedConnection);
}

void AudioVideoCallController::incoming_audio_packet(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	Q_UNUSED(seq_num);
	Q_UNUSED(timestamp);

	if (!is_active || peer_id != m_peer_id)
		return;

	// Direct call, not invokeMethod()/QueuedConnection - see the comment in
	// stopAudioPipeline(). enqueuePacket() is mutex-protected and safe to
	// call from any thread; queuing it behind output's own blocking Play()
	// call would mean it's never delivered, and Play() would sit forever
	// pulling from an empty jitter buffer.
	output->enqueuePacket(data);
}

void AudioVideoCallController::video_capture_started(QString codec, int width, int height)
{
	if (!is_active)
		return;

	// Announced only now, because what the camera turned out to deliver
	// decides it: a compressed camera stream is forwarded in the camera's own
	// codec and at the camera's own size, rather than in ours.
	skynet->SendVideoParams(m_peer_id, codec, (quint16)width, (quint16)height);
}

void AudioVideoCallController::send_video_packet(QByteArray data, bool is_last_fragment)
{
	if (!is_active)
		return;

	// Every fragment of one encoded frame carries the timestamp taken when
	// that frame's first fragment was sent, so the receiving side can tell
	// the fragments of one frame from those of the next.
	if (!m_videoFrameStarted)
	{
		m_videoFrameTimestamp = (quint32)m_callClock.elapsed();
		m_videoFrameStarted = true;
	}

	quint16 seq = m_videoSeq & VideoFrameAssembler::SeqNumMask;
	m_videoSeq = (m_videoSeq + 1) & VideoFrameAssembler::SeqNumMask;

	if (is_last_fragment)
	{
		seq |= VideoFrameAssembler::LastFragmentFlag;
		// The next fragment emitted belongs to a new frame.
		m_videoFrameStarted = false;
	}

	skynet->SendVideoPacket(m_peer_id, seq, m_videoFrameTimestamp, data);
}

void AudioVideoCallController::incoming_video_params(QByteArray peer_id, QString codec, quint16 width, quint16 height)
{
	if (!is_active || peer_id != m_peer_id)
		return;

	QMetaObject::invokeMethod(remotevideo, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, codec), Q_ARG(int, (int)width), Q_ARG(int, (int)height));
	QMetaObject::invokeMethod(remotevideo, "Play", Qt::QueuedConnection);
}

void AudioVideoCallController::incoming_video_packet(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	if (!is_active || peer_id != m_peer_id)
		return;

	QByteArray frame = m_videoAssembler.addFragment(seq_num, timestamp, data);

	// Still waiting for the rest of this frame's fragments.
	if (frame.isEmpty())
		return;

	// Direct call, not invokeMethod()/QueuedConnection - same reason as in
	// incoming_audio_packet().
	remotevideo->enqueueFrame(frame);
}

