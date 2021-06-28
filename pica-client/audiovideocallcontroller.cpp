#include "audiovideocallcontroller.h"
#include "globals.h"
#include "skynet.h"
#include <QByteArray>

AudioVideoCallController::AudioVideoCallController(QObject *parent)
 : QObject(parent), callwindow(0)
{

}

void AudioVideoCallController::start_call(QByteArray peer_id)
{
	if (callwindow)
	{
		/* call is already in progress */
		return;
	}

	callwindow = new CallWindow(peer_id);
	callwindow->show();
}
