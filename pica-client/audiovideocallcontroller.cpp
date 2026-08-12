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

AudioVideoCallController::AudioVideoCallController(QObject *parent)
 : QObject(parent), callwindow(0), ringdevice(0), is_active(false), m_audioSeq(0)
{
	connect(skynet, SIGNAL(IncomingCall(QByteArray)), this, SLOT(call_from(QByteArray)));
	connect(skynet, SIGNAL(CallFailed(QByteArray,QString)), this, SLOT(call_failed(QByteArray,QString)));
	connect(skynet, SIGNAL(CallAccepted(QByteArray)), this, SLOT(call_accepted(QByteArray)));
	connect(skynet, SIGNAL(CallRejected(QByteArray)), this, SLOT(call_rejected(QByteArray)));
	connect(skynet, SIGNAL(CallHungup(QByteArray)), this, SLOT(call_hungup(QByteArray)));

	connect(skynet, SIGNAL(IncomingAudioParams(QByteArray,QString,quint16)), this, SLOT(incoming_audio_params(QByteArray,QString,quint16)));
	connect(skynet, SIGNAL(IncomingAudioPacket(QByteArray,quint16,quint32,QByteArray)), this, SLOT(incoming_audio_packet(QByteArray,quint16,quint32,QByteArray)));

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
}

AudioVideoCallController::~AudioVideoCallController()
{
	stopTones();
	stopAudioPipeline();

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
	QMetaObject::invokeMethod(microphone, "Close", Qt::QueuedConnection);
	QMetaObject::invokeMethod(output, "Close", Qt::QueuedConnection);
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
}

void AudioVideoCallController::end_call()
{
	stopTones();
	stopAudioPipeline();

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

	QMetaObject::invokeMethod(output, "enqueuePacket", Qt::QueuedConnection, Q_ARG(QByteArray, data));
}

