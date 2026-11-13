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
#include "picasystray.h"
#include "skynet.h"
#include "settings.h"
#include "callsettings.h"
#include "contacts.h"
#include "accounts.h"
#include "msguirouter.h"
#include "picaactioncenter.h"
#include <QMenu>
#include <QTimer>
#include <QByteArray>
#include <QDebug>
#include <QMetaObject>


AudioVideoCallController::AudioVideoCallController(QObject *parent)
 : QObject(parent), callwindow(0), ringdevice(0), is_active(false), m_cameraOn(false),
   m_peerAudioRate(0), m_lastAudioSeq(0), m_haveAudioSeq(false), m_audioReceived(0), m_audioLost(0),
   m_audioSeq(0),
   m_videoSeq(0), m_videoFrameTimestamp(0), m_videoFrameStarted(false),
   m_requestedFrameRate(0)
{
	m_qualityTimer = new QTimer(this);
	m_qualityTimer->setInterval(2000);
	connect(m_qualityTimer, SIGNAL(timeout()), this, SLOT(report_quality()));

	connect(action_center, SIGNAL(settingsChanged(QStringList)), this, SLOT(settings_changed(QStringList)));
	connect(skynet, SIGNAL(IncomingCall(QByteArray)), this, SLOT(call_from(QByteArray)));
	connect(skynet, SIGNAL(CallFailed(QByteArray,QString)), this, SLOT(call_failed(QByteArray,QString)));
	connect(skynet, SIGNAL(CallAccepted(QByteArray)), this, SLOT(call_accepted(QByteArray)));
	connect(skynet, SIGNAL(CallRejected(QByteArray)), this, SLOT(call_rejected(QByteArray)));
	connect(skynet, SIGNAL(CallHungup(QByteArray)), this, SLOT(call_hungup(QByteArray)));

	connect(skynet, SIGNAL(IncomingAudioParams(QByteArray,QString,quint16)), this, SLOT(incoming_audio_params(QByteArray,QString,quint16)));
	connect(skynet, SIGNAL(IncomingAudioPacket(QByteArray,quint16,quint32,QByteArray)), this, SLOT(incoming_audio_packet(QByteArray,quint16,quint32,QByteArray)));

	connect(skynet, SIGNAL(IncomingVideoParams(QByteArray,QString,quint16,quint16)), this, SLOT(incoming_video_params(QByteArray,QString,quint16,quint16)));
	connect(skynet, SIGNAL(IncomingVideoPacket(QByteArray,quint16,quint32,QByteArray)), this, SLOT(incoming_video_packet(QByteArray,quint16,quint32,QByteArray)));

	connect(skynet, SIGNAL(CallMediaTransportChanged(QByteArray,bool,QString,quint32)), this, SLOT(media_transport_changed(QByteArray,bool,QString,quint32)));

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

	// Queued rather than direct: this crosses from the capture thread, and
	// warn_capture_clipping() only logs today but is where a UI notice would
	// go.
	connect(microphone, SIGNAL(captureClipping(double,double)), this,
	        SLOT(capture_clipping(double,double)), Qt::QueuedConnection);
	microphone_thread.start();

	output = new AudioDevice();
	output->moveToThread(&output_thread);
	output_thread.start();

	// Camera capture+encode and remote video decode - same arrangement as
	// the audio devices above.
	cam = new VideoDevice();
	cam->moveToThread(&cam_thread);
	connect(cam, SIGNAL(captureStarted(QString,int,int,double)), this, SLOT(video_capture_started(QString,int,int,double)));
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

void AudioVideoCallController::setupEchoCanceller()
{
	const int sampleRate = callAudioSampleRate(m_cs.audioCodec);

	// One canceller for the call, shared by both directions: the playback
	// side feeds it what it is about to play, the capture side subtracts the
	// echo of that from the microphone. Ownership is shared because
	// stopAudioPipeline() does not join either thread, so this has to outlive
	// whichever of them returns last.
	//
	// Only for EchoCancellationOwn. The other two settings mean something
	// below us is doing the job or nobody is, and either way running this as
	// well would only take a second bite out of the speech - the devices are
	// opened accordingly, see AudioDevice::captureNative().
	//
	// Built at the rate the codec runs at rather than at a fixed one, so that
	// choosing a codec does not quietly switch cancellation off: the capture
	// side drops a canceller whose rate does not match its own.
	if (AudioDevice::EchoCancellation() == EchoCancellationOwn)
		m_echoCanceller = EchoCancellerPtr(new EchoCanceller(sampleRate));
	else
		m_echoCanceller.clear();

	if (m_echoCanceller && !m_echoCanceller->isValid())
		m_echoCanceller.clear();

	// Set before the blocking loops are started, so the queued Capture()/
	// Play() calls below are what publishes it to those threads.
	microphone->setEchoCanceller(m_echoCanceller);
	output->setEchoCanceller(m_echoCanceller);
}

void AudioVideoCallController::startAudioPipeline()
{
	m_cs = CallSettings::load();

	// What we capture and encode at, announced to the peer in the 0x74
	// message. It follows from the codec - see callAudioSampleRate().
	//
	// The playback direction is NOT this: it follows whatever the peer
	// announces in its own 0x74 (see incoming_audio_params()), so a call with
	// a peer that picked a different codec or rate keeps working.
	const int sampleRate = callAudioSampleRate(m_cs.audioCodec);
	const CallSettings &cs = m_cs;

	m_audioSeq = 0;
	m_callClock.start();
	microphone->setMuted(false); // a new call never starts muted

	setupEchoCanceller();

	// Declare our outgoing codec to the peer, then start capturing and
	// encoding right away - the peer's decoder is only ready once it has
	// processed this 0x74 message, but since the call is carried over TCP,
	// packets sent immediately after are guaranteed to arrive after it.
	skynet->SendAudioParams(m_peer_id, cs.audioCodec, (quint16)sampleRate);

	startMicrophone();

	m_announcedVideo.clear();
	m_peerAudioCodec.clear();
	m_haveAudioSeq = false;
	m_audioReceived = m_audioLost = 0;
	m_qualityTimer->start();

	// output is configured lazily, once the peer's own 0x74 (see
	// incoming_audio_params()) tells us what codec/rate it is sending.
}

// Also what switches to another microphone during a call. The codec and rate
// are the ones already announced to the peer, so nothing is sent.
void AudioVideoCallController::startMicrophone()
{
	Settings st(config_dbname);
	QString capDev = st.loadValue("audio.capture_device", "default").toString();
	const CallSettings &cs = m_cs;
	const int sampleRate = callAudioSampleRate(cs.audioCodec);
	const int bitrate = callAudioBitrateAdjustable(cs.audioCodec)
	                    ? cs.audioBitrateKbps * 1000
	                    : 0;

	QMetaObject::invokeMethod(microphone, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, capDev), Q_ARG(QString, cs.audioCodec),
	                           Q_ARG(int, sampleRate), Q_ARG(int, bitrate));
	QMetaObject::invokeMethod(microphone, "Capture", Qt::QueuedConnection);
}

void AudioVideoCallController::startPlayback()
{
	QString playDev = Settings(config_dbname).loadValue("audio.playback_device", "default").toString();

	QMetaObject::invokeMethod(output, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, playDev), Q_ARG(QString, m_peerAudioCodec), Q_ARG(int, m_peerAudioRate));
	QMetaObject::invokeMethod(output, "Play", Qt::QueuedConnection);
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
	m_qualityTimer->stop();

	// Drop our reference. The two audio threads still hold theirs until they
	// come out of Capture()/Play(), and the canceller goes away with the last
	// of them.
	m_echoCanceller.clear();
}

void AudioVideoCallController::startVideoPipeline()
{
	Settings st(config_dbname);
	QString camDev = st.loadValue("video.capture_device", QString()).toString();
	const CallSettings &cs = m_cs;

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
	m_cameraOn = true;

	// Datagram sized from the start, even though the call begins on the c2c
	// connection where a 0x77 message could hold far more: a media connection
	// may come up at any moment during the call, and frames whose fragments
	// were cut before that would not fit into a datagram. Over TCP this only
	// costs a few more message headers.
	cam->setMaxFragmentSize(PICA_MEDIA_SAFE_PAYLOAD);

	bool preferCompressed = st.loadValue("video.prefer_compressed", 0).toBool();

	// Kept so that video_capture_started() can tell whether the camera
	// honoured it, which is not something the camera reports.
	m_requestedFrameRate = cs.captureFrameRate;

	// The peer is told what we are sending only once the camera is open and
	// the format is settled - with a compressed camera stream being forwarded
	// as-is, the codec is the camera's choice, not ours (see
	// video_capture_started()).
	QMetaObject::invokeMethod(cam, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, camDev),
	                           Q_ARG(int, cs.captureWidth), Q_ARG(int, cs.captureHeight),
	                           Q_ARG(int, cs.captureFrameRate),
	                           Q_ARG(bool, preferCompressed),
	                           Q_ARG(QString, cs.videoCodec), Q_ARG(int, cs.videoBitrateKbps * 1000),
	                           Q_ARG(int, cs.videoEncodingAcceleration));
	QMetaObject::invokeMethod(cam, "Capture", Qt::QueuedConnection);

	// remotevideo is configured lazily, once the peer's own 0x75 (see
	// incoming_video_params()) tells us what it is sending.
}

void AudioVideoCallController::stopVideoPipeline()
{
	// Direct calls, for the same reason stopAudioPipeline() uses them.
	m_cameraOn = false;
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

	if (callwindow)
		callwindow->setStatus(reason);
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

	if (callwindow->cameraOn())
		startVideoPipeline();
}

void AudioVideoCallController::call_rejected(QByteArray peer_id)
{
	if (peer_id != m_peer_id)
		return;

	if (callwindow)
		callwindow->setStatus(tr("Busy or declined"));

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
	connect_callwindow();
	callwindow->show();
}

void AudioVideoCallController::connect_callwindow()
{
	connect(callwindow, SIGNAL(callwindow_closed(CallWindow*)), this, SLOT(callwindow_closed(CallWindow*)));
	/* decoded frames arrive from remotevideo's thread, so this is a queued
	   connection; it dies with the window, which is deleted on close */
	connect(remotevideo, SIGNAL(frameReady(QImage)), callwindow, SLOT(showRemoteFrame(QImage)));
	connect(cam, SIGNAL(previewReady(QImage)), callwindow, SLOT(showLocalFrame(QImage)));
	connect(callwindow, SIGNAL(video_area_changed(QSize)), this, SLOT(video_area_changed(QSize)));
	connect(callwindow, SIGNAL(mute_toggled(bool)), this, SLOT(mute_toggled(bool)));
	connect(callwindow, SIGNAL(camera_toggled(bool)), this, SLOT(camera_toggled(bool)));
	connect(callwindow->devicesMenu(), SIGNAL(aboutToShow()), this, SLOT(fill_devices_menu()));
#ifdef HAVE_VAAPI
	connect(remotevideo, SIGNAL(hwFrameReady(AVFramePtr)), callwindow, SLOT(showRemoteHwFrame(AVFramePtr)));
	connect(callwindow, SIGNAL(video_rendering_failed()), this, SLOT(video_rendering_failed()));
#endif
}

void AudioVideoCallController::mute_toggled(bool muted)
{
	// direct call: the microphone's thread is blocked inside Capture(), see stopAudioPipeline()
	microphone->setMuted(muted);
}

void AudioVideoCallController::camera_toggled(bool on)
{
	// before the call is up the button only says how it should start
	if (!is_active || on == m_cameraOn)
		return;

	if (on)
		startVideoPipeline();
	else
	{
		// Nothing is sent to say so: the peer's call window hides the video
		// when frames stop coming, and its decoder stays up for when they
		// come back. A second 0x75 would be the alternative, and clients from
		// before this one cannot take one in mid-call.
		m_cameraOn = false;
		cam->Close();
	}
}

void AudioVideoCallController::fill_devices_menu()
{
	QMenu *menu = callwindow ? callwindow->devicesMenu() : nullptr;

	if (!menu)
		return;

	static const char *titles[] = {QT_TR_NOOP("Microphone"), QT_TR_NOOP("Speaker"), QT_TR_NOOP("Camera")};
	static const char *keys[] = {"audio.capture_device", "audio.playback_device", "video.capture_device"};
	Settings st(config_dbname);
	AudioDevice audio;
	VideoDevice video;

	menu->clear();

	for (int kind = 0; kind < 3; kind++)
	{
		QList<MediaDeviceInfo> devs = kind == 0 ? audio.Enumerate(CAPTURE)
		                              : kind == 1 ? audio.Enumerate(PLAYBACK)
		                              : video.Enumerate(CAPTURE);
		QString current = st.loadValue(keys[kind], kind == 2 ? QString() : QString("default")).toString();
		QMenu *sub = menu->addMenu(tr(titles[kind]));

		for (int i = 0; i < devs.size(); i++)
		{
			QString device = devs[i].device;
			QAction *a = sub->addAction(devs[i].humanReadable.isEmpty() ? device : devs[i].humanReadable);

			a->setCheckable(true);
			a->setChecked(device == current);
			connect(a, &QAction::triggered, this, [this, kind, device] { device_selected(kind, device); });
		}

		sub->setEnabled(!devs.isEmpty());
	}
}

void AudioVideoCallController::device_selected(int kind, QString device)
{
	static const char *keys[] = {"audio.capture_device", "audio.playback_device", "video.capture_device"};

	Settings(config_dbname).storeValue(keys[kind], device);
	action_center->notifySettingsChanged(QStringList() << keys[kind]);
}

// A setting changed - in the settings window, the call window's device menu or
// by automatic configuration. Outside a call there is nothing to do: every call
// reads its settings when it starts. During one, what can change without the
// peer having to be told is applied by restarting the device concerned.
// Close() makes the device's blocking loop return, after which its thread runs
// the queued configure and start calls.
void AudioVideoCallController::settings_changed(QStringList names)
{
	if (!is_active)
		return;

	const bool audioSystem = names.contains("audio.driver") || names.contains("audio.echo_cancellation");
	const CallSettings now = CallSettings::load();

	// Bitrates only. A new codec or picture format would have to be announced,
	// which clients from before this one cannot take during a call.
	m_cs.audioBitrateKbps = now.audioBitrateKbps;
	m_cs.videoBitrateKbps = now.videoBitrateKbps;

	// both directions share one canceller, so it changes for both or neither
	if (audioSystem)
		setupEchoCanceller();

	if (audioSystem || names.contains("audio.capture_device") || names.contains("call.audio_bitrate_kbps"))
	{
		microphone->Close();
		startMicrophone();
	}

	if ((audioSystem || names.contains("audio.playback_device")) && !m_peerAudioCodec.isEmpty())
	{
		output->Close();
		startPlayback();
	}

	if ((names.contains("video.capture_device") || names.contains("call.video_bitrate_kbps")) && m_cameraOn)
	{
		cam->Close();
		startVideoPipeline();
	}
}

void AudioVideoCallController::report_quality()
{
	int total = m_audioReceived + m_audioLost;

	// Only what went missing on the way here. A call relayed over the chat
	// connection is carried by TCP and loses nothing, it is late instead.
	if (callwindow && total > 0)
		callwindow->setQuality(m_audioLost * 100 / total);

	m_audioReceived = 0;
	m_audioLost = 0;
}

void AudioVideoCallController::call_from(QByteArray peer_id)
{
	if (callwindow)
	{
		/* busy: one call at a time. The same peer calling again is left to
		   the window that is already open for it. */
		if (peer_id != m_peer_id)
		{
			QString name = Contacts(config_dbname, Accounts::GetCurrentAccount().id).GetContactName(peer_id);

			skynet->RejectCall(peer_id);
			systray->ShowMessage(tr("Missed call"), name.isEmpty() ? QString(peer_id.toBase64().left(12)) + "..." : name);
		}
		return;
	}

	m_peer_id = peer_id;
	callwindow = new CallWindow(peer_id, true);
	connect(callwindow, SIGNAL(accept_call_pressed()), this, SLOT(accept_call()));
	connect(callwindow, SIGNAL(hang_call_pressed()), this, SLOT(end_call()));
	connect_callwindow();
	callwindow->show();
	callwindow->raise();
	callwindow->activateWindow();
	systray->ShowMessage(tr("Incoming call"), callwindow->windowTitle());

	/* play ring tone */
	playRingTone();
}

void AudioVideoCallController::initiate_call()
{
	skynet->StartCall(m_peer_id);
	callwindow->setStatus(tr("Ringing..."));
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

	if (callwindow->cameraOn())
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

// Rate limited by AudioDevice, which reports at most once every couple of
// seconds of audio, so this can log unconditionally.
//
// Only a warning, and deliberately: clipping happens in the converter, before
// anything here sees a sample, so nothing in this process can undo it. The
// fix is less gain ahead of the ADC, which is the capture device's own mixer
// control and the user's to set.
void AudioVideoCallController::capture_clipping(double pinnedPercent, double peakDb)
{
	qWarning("Microphone is clipping: %.2f%% of samples at full scale, peak %.1f dBFS - "
	         "turn the recording level down, it distorts the speech and stops the echo "
	         "canceller converging",
	         pinnedPercent, peakDb);
}


void AudioVideoCallController::incoming_audio_params(QByteArray peer_id, QString codec, quint16 sample_rate)
{
	if (!is_active || peer_id != m_peer_id)
		return;

	// The codec name is peer-supplied and would otherwise be handed straight
	// to the FFmpeg decoder - accept only the ones the client itself sends.
	if (!callAudioCodecAccepted(codec))
	{
		qWarning() << "rejecting unsupported call audio codec from peer:" << codec;
		return;
	}

	m_peerAudioCodec = codec;
	m_peerAudioRate = sample_rate;

	// The queued calls below wait for output's thread, which a playback
	// already running would never give back - a repeated 0x74 would sit in the
	// queue until the call ended and start playing then. Close() first makes
	// that a restart with the new parameters.
	output->Close();
	startPlayback();
}

void AudioVideoCallController::incoming_audio_packet(QByteArray peer_id, quint16 seq_num, quint32 timestamp, QByteArray data)
{
	Q_UNUSED(timestamp);

	if (!is_active || peer_id != m_peer_id)
		return;

	// Direct call, not invokeMethod()/QueuedConnection - see the comment in
	// stopAudioPipeline(). enqueuePacket() is mutex-protected and safe to
	// call from any thread; queuing it behind output's own blocking Play()
	// call would mean it's never delivered, and Play() would sit forever
	// pulling from an empty jitter buffer.
	output->enqueuePacket(seq_num, data);

	// for report_quality(); a gap that large is a reordered or restarted stream, not loss
	if (m_haveAudioSeq)
	{
		quint16 gap = seq_num - m_lastAudioSeq - 1;

		if (gap < 1000)
			m_audioLost += gap;
	}

	m_lastAudioSeq = seq_num;
	m_haveAudioSeq = true;
	m_audioReceived++;
}

void AudioVideoCallController::video_capture_started(QString codec, int width, int height,
                                                     double frameRate)
{
	if (!is_active || !m_cameraOn)
		return;

	// Logged rather than announced: the 0x74 message has no field for it, and
	// the peer does not need one - every frame carries its own timestamp. It
	// is worth a line in the log all the same, since a camera that quietly
	// substituted a lower rate than was asked for is otherwise invisible from
	// the other end of a complaint about choppy video.
	if (frameRate > 0.0 && frameRate < m_requestedFrameRate - 0.5)
		qWarning() << QString("Camera delivering %1 fps, not the %2 that was requested")
		              .arg(frameRate, 0, 'f', 1).arg(m_requestedFrameRate);

	// Announced only now, because what the camera turned out to deliver
	// decides it: a compressed camera stream is forwarded in the camera's own
	// codec and at the camera's own size, rather than in ours.
	QString format = QString("%1 %2x%3").arg(codec).arg(width).arg(height);

	// A camera switched back on, or swapped for one with the same format,
	// carries on in the stream the peer is already decoding.
	if (format == m_announcedVideo)
		return;

	// A second 0x75 in one call restarts the peer's decoder. Clients from
	// before EXTMSG queue that restart behind the running decoder instead,
	// where it stays until the call ends and then blocks their video for good.
	if (!m_announcedVideo.isEmpty() && !skynet->peerHasExt(m_peer_id))
	{
		m_cameraOn = false;
		cam->Close();
		msguirouter->notification(tr("The contact's client cannot follow a change of video format during a call, the camera was switched off"), true);
		return;
	}

	m_announcedVideo = format;
	skynet->SendVideoParams(m_peer_id, codec, (quint16)width, (quint16)height);
}

void AudioVideoCallController::send_video_packet(QByteArray data, bool is_last_fragment)
{
	if (!is_active || !m_cameraOn)
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

void AudioVideoCallController::media_transport_changed(QByteArray peer_id, bool direct_udp, QString ciphersuitename, quint32 max_payload)
{
	if (peer_id != m_peer_id)
		return;

	// Fragments stay datagram sized for the whole call (see
	// startVideoPipeline()); this only lowers the size further, for when the
	// media connection turns out to carry less than was assumed. Direct call,
	// not invokeMethod()/QueuedConnection - see the comment in
	// stopAudioPipeline(): cam's thread is blocked inside Capture().
	cam->setMaxFragmentSize(qMin((int)max_payload, (int)PICA_MEDIA_SAFE_PAYLOAD));

	if (callwindow)
		callwindow->setMediaTransport(direct_udp, ciphersuitename);

	qDebug() << "call media transport:" << (direct_udp ? "direct UDP/DTLS" : "c2c connection")
	         << ciphersuitename << "max payload" << max_payload;
}

void AudioVideoCallController::incoming_video_params(QByteArray peer_id, QString codec, quint16 width, quint16 height)
{
	if (!is_active || peer_id != m_peer_id)
		return;

	// As in incoming_audio_params(): the peer-supplied codec name must be one
	// we actually send before it reaches avcodec_find_decoder_by_name().
	if (!callVideoCodecAccepted(codec))
	{
		qWarning() << "rejecting unsupported call video codec from peer:" << codec;
		return;
	}

	Settings st(config_dbname);
	const CallSettings cs = CallSettings::load();
	bool vaapiRendering = st.loadValue("video.vaapi_rendering", 0).toBool();

#ifdef HAVE_VAAPI
	// A call started after one where drawing GPU frames turned out not to work
	// would otherwise begin with a blank video area all over again.
	if (VaapiRenderWidget::renderingKnownBroken())
		vaapiRendering = false;
#endif

	// As in incoming_audio_params(): a repeated 0x75 restarts the decoder
	// instead of queueing behind the one that is running.
	remotevideo->Close();
	m_videoAssembler.reset();

	QMetaObject::invokeMethod(remotevideo, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, codec), Q_ARG(int, (int)width), Q_ARG(int, (int)height),
	                           Q_ARG(int, cs.videoDecodingAcceleration), Q_ARG(bool, vaapiRendering));
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

void AudioVideoCallController::video_area_changed(QSize area)
{
	// Direct call rather than invokeMethod() - same reason as in
	// incoming_video_packet(): remotevideo's thread is blocked inside Play().
	remotevideo->setDisplaySize(area);
}

#ifdef HAVE_VAAPI
void AudioVideoCallController::video_rendering_failed()
{
	// The window cannot draw frames that stayed in GPU memory, so stop keeping
	// them there: the decoder brings them down into system memory instead and
	// the window's label shows them. Decoding itself is unaffected and the
	// call carries on.
	//
	// Direct call rather than invokeMethod() - same reason as in
	// incoming_video_packet(): remotevideo's thread is blocked inside Play().
	remotevideo->disableHardwareRendering();
}
#endif

