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
 : QObject(parent), callwindow(0), is_active(false)
{
	connect(skynet, SIGNAL(IncomingCall(QByteArray)), this, SLOT(call_from(QByteArray)));
	connect(skynet, SIGNAL(CallFailed(QString)), this, SLOT(call_failed(QString)));
	connect(skynet, SIGNAL(CallAccepted(QByteArray)), this, SLOT(call_accepted(QByteArray)));
	connect(skynet, SIGNAL(CallRejected(QByteArray)), this, SLOT(call_rejected(QByteArray)));
	connect(skynet, SIGNAL(CallHungup(QByteArray)), this, SLOT(call_hungup(QByteArray)));

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
}

AudioVideoCallController::~AudioVideoCallController()
{
	stopTones();

	toneplayer_thread.quit();
	toneplayer_thread.wait();
	delete toneplayer;

	ringtoneplayer_thread.quit();
	ringtoneplayer_thread.wait();
	delete ringtoneplayer;
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

void AudioVideoCallController::call_failed(QString reason)
{
	qDebug() << "Call failed: " << reason << "\n";
	is_active = false;
	/* play unreachable */
	playEarpieceTone(&TonePlayer::playUnreachable);
}

void AudioVideoCallController::call_accepted(QByteArray peer_id)
{
	stopTones();
	is_active = true;
	callwindow->call_started();
}

void AudioVideoCallController::call_rejected(QByteArray peer_id)
{
	/* play busy tone */
	playEarpieceTone(&TonePlayer::playBusy);
}

void AudioVideoCallController::call_hungup(QByteArray peer_id)
{
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
}

void AudioVideoCallController::end_call()
{
	stopTones();

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

