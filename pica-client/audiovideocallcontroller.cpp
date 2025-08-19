#include "audiovideocallcontroller.h"
#include "globals.h"
#include "skynet.h"
#include <QByteArray>
#include <QDebug>

AudioVideoCallController::AudioVideoCallController(QObject *parent)
 : QObject(parent), callwindow(0), is_active(false)
{
	connect(skynet, SIGNAL(IncomingCall(QByteArray)), this, SLOT(call_from(QByteArray)));
	connect(skynet, SIGNAL(CallFailed(QString)), this, SLOT(call_failed(QString)));
	connect(skynet, SIGNAL(CallAccepted(QByteArray)), this, SLOT(call_accepted(QByteArray)));
	connect(skynet, SIGNAL(CallRejected(QByteArray)), this, SLOT(call_rejected(QByteArray)));
	connect(skynet, SIGNAL(CallHungup(QByteArray)), this, SLOT(call_hungup(QByteArray)));
}

void AudioVideoCallController::call_failed(QString reason)
{
	qDebug() << "Call failed: " << reason << "\n";
}

void AudioVideoCallController::call_accepted(QByteArray peer_id)
{
}

void AudioVideoCallController::call_rejected(QByteArray peer_id)
{
}

void AudioVideoCallController::call_hungup(QByteArray peer_id)
{
}

void AudioVideoCallController::start_call(QByteArray peer_id)
{
	if (callwindow)
	{
		/* call is already in progress */
		return;
	}

	m_peer_id = peer_id;
	callwindow = new CallWindow(peer_id, false);
	connect(callwindow, SIGNAL(start_call_pressed()), this, SLOT(initiate_call()));
	connect(callwindow, SIGNAL(hang_call_pressed()), this, SLOT(end_call()));
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
	callwindow->show();

	/* play ring tone */

}

void AudioVideoCallController::initiate_call()
{
	skynet->StartCall(m_peer_id);
}

void AudioVideoCallController::accept_call()
{
	skynet->AcceptCall(m_peer_id);
}

void AudioVideoCallController::end_call()
{
	if (is_active)
		skynet->HangupCall(m_peer_id);
	else
		skynet->RejectCall(m_peer_id);
}

