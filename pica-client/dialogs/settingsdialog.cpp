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
#include "settingsdialog.h"
#include "../settings.h"
#include "../globals.h"
#include "../audiodevice.h"
#include "../videodevice.h"
#include "../../PICA_netconf.h"
#include "../../PICA_proto.h"

#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVariant>
#include <QListWidget>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QTabWidget>
#include <QTimer>

// Audio pipeline test: how long the microphone is recorded for, and the
// sample rate to record at - the same rate a call negotiates.
static const int kAudioTestRecordMs = 5000;
static const int kAudioTestSampleRate = 48000;

// One encoded packet covers this much audio (the Opus encoder produces
// 20 ms frames at 48 kHz), so feeding one packet per this many milliseconds
// replays the recording at its natural rate.
static const int kAudioTestPacketMs = 20;

// Grace period after the last packet has been handed over, for the jitter
// buffer and the sound card's own buffer to drain before closing the device.
static const int kAudioTestDrainMs = 500;

// Size requested from the camera by the video pipeline test, matching what a
// call asks for.
static const int kVideoTestWidth = 640;
static const int kVideoTestHeight = 480;

SettingsDialog::SettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	QVBoxLayout *settingsLayout = new QVBoxLayout();

	QTabWidget *tabW = new QTabWidget(this);
	QWidget* directc2ctab = new QWidget(0);
	QWidget* soundstab = new QWidget(0);
	QWidget* multilogintab = new QWidget(0);
	QWidget* audiodevtab = new QWidget(0);
	QWidget* videodevtab = new QWidget(0);

	QVBoxLayout *directc2cLayout = new QVBoxLayout();
	QVBoxLayout *multiloginlayout = new QVBoxLayout();
	QVBoxLayout *audiodevlayout = new QVBoxLayout();
	QVBoxLayout *videodevLayout = new QVBoxLayout();

//Direct connections
	rbDisableDirectConns = new QRadioButton(tr("Disable direct connections"), this);
	rbEnableOutgoingConns = new QRadioButton(tr("Connect to the remote peer directly if possible"), this);
	rbEnableIncomingConns = new QRadioButton(tr("Enable incoming direct connections"), this);

#ifdef HAVE_LIBMINIUPNPC
	cbEnableUPnP = new QCheckBox(tr("Enable UPnP"));
	cbEnableUPnP->setChecked(true);
#endif

	rbEnableIncomingConns->setChecked(true);
	connect(rbEnableIncomingConns, SIGNAL(toggled(bool)), this, SLOT(toggleIncomingConnections(bool)));

	QLabel *lbAddr = new QLabel(tr("Public address for incoming connections"), this);
	addr = new QComboBox(this);
	QLabel *lbPubPort = new QLabel(tr("External TCP port for incoming connections"), this);
	publicPort = new QSpinBox(this);
	QLabel *lbLocPort = new QLabel(tr("Local TCP port for incoming connections"), this);
	localPort = new QSpinBox(this);

	addr->setEditable(true);

	publicPort->setRange(1, 65535);
	localPort->setRange(1, 65535);

	publicPort->setValue(2298);
	localPort->setValue(2298);

	QList<QHostAddress> hostIfAddrs = QNetworkInterface::allAddresses();
	QHostAddress ifAddr;

	addr->addItem(QString("autoconfigure"));

	foreach (ifAddr, hostIfAddrs)
		if (ifAddr.protocol() == QAbstractSocket::IPv4Protocol)
			addr->addItem(ifAddr.toString());


	directc2cLayout->addWidget(rbDisableDirectConns);
	directc2cLayout->addWidget(rbEnableOutgoingConns);
	directc2cLayout->addWidget(rbEnableIncomingConns);
#ifdef HAVE_LIBMINIUPNPC
	directc2cLayout->addWidget(cbEnableUPnP);
#endif
	directc2cLayout->addWidget(lbAddr);
	directc2cLayout->addWidget(addr);
	directc2cLayout->addWidget(lbPubPort);
	directc2cLayout->addWidget(publicPort);
	directc2cLayout->addWidget(lbLocPort);
	directc2cLayout->addWidget(localPort);
	directc2cLayout->addStretch(1);

	directc2ctab->setLayout(directc2cLayout);

//Multiple logins
	QLabel *lbMLP = new QLabel("Policy for same account logins from multiple devices");
	rbMLPProhibit = new QRadioButton(tr("Prohibit new login attempts"), this);
	rbMLPReplace = new QRadioButton(tr("Replace existing connections"), this);
	rbMLPAllowMultiple = new QRadioButton(tr("Allow multiple logins, try to synchronise chat history"));

	rbMLPProhibit->setChecked(true);

	multiloginlayout->addWidget(lbMLP);
	multiloginlayout->addWidget(rbMLPProhibit);
	multiloginlayout->addWidget(rbMLPReplace);
	multiloginlayout->addWidget(rbMLPAllowMultiple);
	multiloginlayout->addStretch(1);

	multilogintab->setLayout(multiloginlayout);

// Audio Devices
	QLabel *lbAudioCaptureDev = new QLabel(tr("Microphone Device 🎙️"));
	QLabel *lbAudioPlaybackDev = new QLabel(tr("Playback device 🎧"));
	QLabel *lbAudioRingDev = new QLabel(tr("Ring device 🔔☎️"));

	audioCaptureDev = new QComboBox(this);
	audioCaptureDev->setMinimumHeight(audioCaptureDev->height() * 2);
	fillAudioCaptureDevices();

	audioPlaybackDev = new QComboBox(this);
	audioPlaybackDev->setMinimumHeight(audioPlaybackDev->height() * 2);
	fillAudioPlaybackDevices();

	audioRingDev = new QComboBox(this);
	audioRingDev->setMinimumHeight(audioRingDev->height() * 2);
	fillAudioRingDevices();

	btAudioTest = new QPushButton(tr("Test 🎙️"), this);
	connect(btAudioTest, SIGNAL(clicked()), this, SLOT(toggleAudioTest()));

	audioTestStatus = new QLabel(this);
	audioTestStatus->setAlignment(Qt::AlignCenter);

	btRingTest = new QPushButton(tr("Test ring 🔔"), this);
	connect(btRingTest, SIGNAL(clicked()), this, SLOT(toggleRingTest()));

	ringTestStatus = new QLabel(this);
	ringTestStatus->setAlignment(Qt::AlignCenter);

	// The microphone test covers the capture and playback devices, so it sits
	// with them, above the unrelated ring device below.
	audiodevlayout->addWidget(lbAudioCaptureDev);
	audiodevlayout->addWidget(audioCaptureDev);
	audiodevlayout->addWidget(lbAudioPlaybackDev);
	audiodevlayout->addWidget(audioPlaybackDev);
	audiodevlayout->addWidget(btAudioTest);
	audiodevlayout->addWidget(audioTestStatus);
	audiodevlayout->addWidget(lbAudioRingDev);
	audiodevlayout->addWidget(audioRingDev);
	audiodevlayout->addWidget(btRingTest);
	audiodevlayout->addWidget(ringTestStatus);
	audiodevlayout->addStretch(1);

	audiodevtab->setLayout(audiodevlayout);

	// Capture+encode and decode+playback ends of the test, each blocking its
	// own thread while running, exactly as they do during a call. They stay
	// idle until the Test button is pressed.
	audioTestState = AudioTestIdle;
	audioTestPlaybackPos = 0;

	audioTestRecordTimer = new QTimer(this);
	audioTestRecordTimer->setSingleShot(true);
	connect(audioTestRecordTimer, SIGNAL(timeout()), this, SLOT(audioTestRecordingFinished()));

	audioTestPlaybackTimer = new QTimer(this);
	connect(audioTestPlaybackTimer, SIGNAL(timeout()), this, SLOT(audioTestFeedPacket()));

	audioTestDrainTimer = new QTimer(this);
	audioTestDrainTimer->setSingleShot(true);
	connect(audioTestDrainTimer, SIGNAL(timeout()), this, SLOT(audioTestPlaybackFinished()));

	testMic = new AudioDevice();
	testMic->moveToThread(&testMicThread);
	connect(testMic, SIGNAL(packetReady(QByteArray)), this, SLOT(audioTestPacket(QByteArray)));
	connect(testMic, SIGNAL(errorOccurred(QString)), this, SLOT(audioTestError(QString)));
	testMicThread.start();

	testSpeaker = new AudioDevice();
	testSpeaker->moveToThread(&testSpeakerThread);
	connect(testSpeaker, SIGNAL(errorOccurred(QString)), this, SLOT(audioTestError(QString)));
	testSpeakerThread.start();

	ringTestRunning = false;
	ringTestPlayer = new TonePlayer();
	ringTestPlayer->moveToThread(&ringTestPlayerThread);
	connect(ringTestPlayer, SIGNAL(finishedPlaying()), this, SLOT(ringTestFinished()));
	connect(ringTestPlayer, SIGNAL(errorOccured(QString)), this, SLOT(ringTestError(QString)));
	ringTestPlayerThread.start();

//Video Devices
	videoDev = new QComboBox(this);
	videoDev->setMinimumHeight(videoDev->height() * 2);
	fillVideoDevices();
	videoDevRefresh = new QPushButton(tr("Refresh 🔄"), this);
	connect(videoDevRefresh, SIGNAL(clicked()), this, SLOT(fillVideoDevices()));

	cbPreferCompressed = new QCheckBox(tr("Prefer compressed formats if provided by the camera"), this);

	btVideoTest = new QPushButton(tr("Test 📷"), this);
	connect(btVideoTest, SIGNAL(clicked()), this, SLOT(toggleVideoTest()));

	videoPreview = new QLabel(this);
	videoPreview->setAlignment(Qt::AlignCenter);
	videoPreview->setMinimumSize(320, 240);
	videoPreview->setFrameShape(QFrame::StyledPanel);

	videodevLayout->addWidget(videoDev);
	videodevLayout->addWidget(videoDevRefresh);
	videodevLayout->addWidget(cbPreferCompressed);
	videodevLayout->addWidget(btVideoTest);
	videodevLayout->addWidget(videoPreview);
	videodevLayout->addStretch(1);

	videodevtab->setLayout(videodevLayout);

	// Capture+encode and decode ends of the test pipeline, each blocking its
	// own thread while running, exactly as they do during a call. They stay
	// idle until the Test button is pressed.
	videoTestRunning = false;

	testCam = new VideoDevice();
	testCam->moveToThread(&testCamThread);
	connect(testCam, SIGNAL(captureStarted(QString,int,int)), this, SLOT(videoTestCaptureStarted(QString,int,int)));
	connect(testCam, SIGNAL(packetReady(QByteArray,bool)), this, SLOT(videoTestFragment(QByteArray,bool)));
	connect(testCam, SIGNAL(errorOccurred(QString)), this, SLOT(videoTestError(QString)));
	testCamThread.start();

	testDecoder = new VideoDevice();
	testDecoder->moveToThread(&testDecoderThread);
	connect(testDecoder, SIGNAL(frameReady(QImage)), this, SLOT(videoTestFrame(QImage)));
	connect(testDecoder, SIGNAL(errorOccurred(QString)), this, SLOT(videoTestError(QString)));
	testDecoderThread.start();

	tabW->addTab(directc2ctab, tr("Direct Connections"));
	tabW->addTab(multilogintab, tr("Multiple logins"));
	tabW->addTab(audiodevtab, tr("Audio Devices"));
	tabW->addTab(videodevtab, tr("Video Devices"));
	tabW->addTab(soundstab, tr("Sounds"));
	settingsLayout->addWidget(tabW);

	QHBoxLayout *buttonsLayout = new QHBoxLayout;

	btOk = new QPushButton(tr("&OK"), this);
	btCancel = new QPushButton(tr("Cancel"), this);

	buttonsLayout->addStretch(1);
	buttonsLayout->addWidget(btOk);
	buttonsLayout->addWidget(btCancel);

	settingsLayout->addLayout(buttonsLayout);

	setLayout(settingsLayout);

	connect(btOk, SIGNAL(clicked()), this, SLOT(OK()));
	connect(btCancel, SIGNAL(clicked()), this, SLOT(Cancel()));

	loadSettings();

	setWindowTitle(tr("Pica Pica Messenger Settings"));
}

SettingsDialog::~SettingsDialog()
{
	// Stops the loops first - the threads cannot finish while Capture()/Play()
	// are still running inside them.
	testCam->Close();
	testDecoder->Close();
	testMic->Close();
	testSpeaker->Close();

	testCamThread.quit();
	testCamThread.wait();
	delete testCam;

	testDecoderThread.quit();
	testDecoderThread.wait();
	delete testDecoder;

	testMicThread.quit();
	testMicThread.wait();
	delete testMic;

	testSpeakerThread.quit();
	testSpeakerThread.wait();
	delete testSpeaker;

	// Same reasoning: stop() first, since the thread cannot finish while
	// TonePlayer::play() is still running inside it.
	ringTestPlayer->stop();
	ringTestPlayerThread.quit();
	ringTestPlayerThread.wait();
	delete ringTestPlayer;
}

void SettingsDialog::toggleRingTest()
{
	if (ringTestRunning)
	{
		// stop() only sets an atomic flag, so it is safe to call directly on
		// a TonePlayer whose thread is blocked inside play().
		ringTestPlayer->stop();
		ringTestRunning = false;
		btRingTest->setText(tr("Test ring 🔔"));
		ringTestStatus->clear();
		return;
	}

	QString dev = audioRingDev->itemData(audioRingDev->currentIndex()).toString();

	if (dev.isEmpty())
	{
		ringTestStatus->setText(tr("No ring device selected"));
		return;
	}

	ringTestRunning = true;
	btRingTest->setText(tr("Stop ⏹"));
	ringTestStatus->setText(tr("Ringing..."));

	// Same sequence AudioVideoCallController::playRingTone() uses for a real
	// incoming call: clear any tone still playing, point the player at the
	// configured device, then start the bell.
	ringTestPlayer->stop();
	QMetaObject::invokeMethod(ringTestPlayer, "setDeviceName", Qt::QueuedConnection, Q_ARG(QString, dev));
	QMetaObject::invokeMethod(ringTestPlayer, "playClassicRingtone", Qt::QueuedConnection);
}

void SettingsDialog::ringTestFinished()
{
	if (!ringTestRunning)
		return;

	ringTestRunning = false;
	btRingTest->setText(tr("Test ring 🔔"));
	ringTestStatus->clear();
}

void SettingsDialog::ringTestError(QString message)
{
	ringTestRunning = false;
	btRingTest->setText(tr("Test ring 🔔"));
	ringTestStatus->setText(message);
}

void SettingsDialog::toggleAudioTest()
{
	if (audioTestState != AudioTestIdle)
	{
		stopAudioTest();
		audioTestStatus->setText(tr("Stopped"));
		return;
	}

	QString dev = audioCaptureDev->itemData(audioCaptureDev->currentIndex()).toString();

	if (dev.isEmpty())
	{
		audioTestStatus->setText(tr("No microphone selected"));
		return;
	}

	audioTestPackets.clear();
	audioTestPlaybackPos = 0;
	audioTestState = AudioTestRecording;
	btAudioTest->setText(tr("Stop ⏹"));
	audioTestStatus->setText(tr("Recording, say something..."));

	QMetaObject::invokeMethod(testMic, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, dev), Q_ARG(QString, QStringLiteral("opus")),
	                           Q_ARG(int, kAudioTestSampleRate));
	QMetaObject::invokeMethod(testMic, "Capture", Qt::QueuedConnection);

	audioTestRecordTimer->start(kAudioTestRecordMs);
}

void SettingsDialog::audioTestPacket(QByteArray data)
{
	// Packets still in flight from the microphone thread after the recording
	// phase ended are not part of the recording.
	if (audioTestState != AudioTestRecording)
		return;

	audioTestPackets.append(data);
}

void SettingsDialog::audioTestRecordingFinished()
{
	// The test may have been stopped, or have failed, while the timer ran.
	if (audioTestState != AudioTestRecording)
		return;

	// Direct call: the microphone thread is blocked inside Capture(), so a
	// queued call would never reach it (see the comments in
	// AudioVideoCallController::stopAudioPipeline()).
	testMic->Close();

	if (audioTestPackets.isEmpty())
	{
		stopAudioTest();
		audioTestStatus->setText(tr("Nothing was captured"));
		return;
	}

	startAudioTestPlayback();
}

void SettingsDialog::startAudioTestPlayback()
{
	QString dev = audioPlaybackDev->itemData(audioPlaybackDev->currentIndex()).toString();

	if (dev.isEmpty())
	{
		stopAudioTest();
		audioTestStatus->setText(tr("No playback device selected"));
		return;
	}

	audioTestState = AudioTestPlaying;
	audioTestPlaybackPos = 0;
	audioTestStatus->setText(tr("Playing back..."));

	QMetaObject::invokeMethod(testSpeaker, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, dev), Q_ARG(QString, QStringLiteral("opus")),
	                           Q_ARG(int, kAudioTestSampleRate));
	QMetaObject::invokeMethod(testSpeaker, "Play", Qt::QueuedConnection);

	// The recording has to be handed over at the rate it plays at, not all at
	// once: AudioDevice's jitter buffer holds only a few packets and drops
	// the oldest when full, so dumping the whole recording into it would
	// discard everything but the tail.
	audioTestPlaybackTimer->start(kAudioTestPacketMs);
}

void SettingsDialog::audioTestFeedPacket()
{
	if (audioTestState != AudioTestPlaying)
		return;

	if (audioTestPlaybackPos >= audioTestPackets.size())
	{
		audioTestPlaybackTimer->stop();
		// Let what is already queued, plus the sound card's own buffer, play
		// out before tearing the playback device down.
		audioTestDrainTimer->start(kAudioTestDrainMs);
		return;
	}

	// Direct call, for the same reason as in the call controller.
	testSpeaker->enqueuePacket(audioTestPackets.at(audioTestPlaybackPos++));
}

void SettingsDialog::audioTestPlaybackFinished()
{
	if (audioTestState != AudioTestPlaying)
		return;

	stopAudioTest();
	audioTestStatus->setText(tr("Done"));
}

void SettingsDialog::stopAudioTest()
{
	audioTestRecordTimer->stop();
	audioTestPlaybackTimer->stop();
	audioTestDrainTimer->stop();

	// Direct calls: both devices may be blocked inside their capture/playback
	// loops, where a queued call would never reach them.
	testMic->Close();
	testSpeaker->Close();

	audioTestPackets.clear();
	audioTestPlaybackPos = 0;
	audioTestState = AudioTestIdle;
	btAudioTest->setText(tr("Test 🎙️"));
	audioTestStatus->clear();
}

void SettingsDialog::audioTestError(QString message)
{
	if (audioTestState == AudioTestIdle)
		return;

	stopAudioTest();
	audioTestStatus->setText(message);
}

void SettingsDialog::toggleVideoTest()
{
	if (videoTestRunning)
	{
		stopVideoTest();
		return;
	}

	QString dev = videoDev->itemData(videoDev->currentIndex()).toString();

	if (dev.isEmpty())
	{
		videoPreview->setText(tr("No camera selected"));
		return;
	}

	testAssembler.reset();
	videoTestRunning = true;
	btVideoTest->setText(tr("Stop ⏹"));
	videoPreview->setText(tr("Starting..."));

	QMetaObject::invokeMethod(testCam, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, dev),
	                           Q_ARG(int, kVideoTestWidth), Q_ARG(int, kVideoTestHeight),
	                           Q_ARG(bool, cbPreferCompressed->isChecked()));
	QMetaObject::invokeMethod(testCam, "Capture", Qt::QueuedConnection);

	// The decoder is only started once the camera has settled on a format,
	// which is what captureStarted() reports - the same order a call follows,
	// where the peer's 0x75 message arrives before its first video packet.
}

void SettingsDialog::videoTestCaptureStarted(QString codec, int width, int height)
{
	if (!videoTestRunning)
		return;

	// Says which path the test is exercising: the camera's own compressed
	// stream forwarded untouched, or frames decoded and re-encoded here.
	videoPreview->setText(tr("Capturing %1 %2x%3...").arg(codec).arg(width).arg(height));

	QMetaObject::invokeMethod(testDecoder, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, codec),
	                           Q_ARG(int, width), Q_ARG(int, height));
	QMetaObject::invokeMethod(testDecoder, "Play", Qt::QueuedConnection);
}

void SettingsDialog::stopVideoTest()
{
	// Direct calls: both devices are blocked inside their capture/decode
	// loops, so a queued call would never reach them (see the comments in
	// AudioVideoCallController::stopAudioPipeline()).
	testCam->Close();
	testDecoder->Close();

	testAssembler.reset();
	videoTestRunning = false;
	btVideoTest->setText(tr("Test 📷"));
	videoPreview->clear();
}

void SettingsDialog::videoTestFragment(QByteArray data, bool is_last_fragment)
{
	if (!videoTestRunning)
		return;

	// The encoder emits fragments exactly as they would go onto the wire, so
	// feed them through the same assembler the receiving end of a call uses.
	// The sequence number is rebuilt here the way send_video_packet() builds
	// it, the timestamp being constant since only frame boundaries matter.
	quint16 seq = is_last_fragment ? VideoFrameAssembler::LastFragmentFlag : 0;
	QByteArray frame = testAssembler.addFragment(seq, 0, data);

	if (frame.isEmpty())
		return;

	// Direct call, for the same reason as in the call controller.
	testDecoder->enqueueFrame(frame);
}

void SettingsDialog::videoTestFrame(QImage frame)
{
	if (!videoTestRunning || frame.isNull())
		return;

	videoPreview->setPixmap(QPixmap::fromImage(frame).scaled(videoPreview->size(),
	                                                         Qt::KeepAspectRatio,
	                                                         Qt::SmoothTransformation));
}

void SettingsDialog::videoTestError(QString message)
{
	if (!videoTestRunning)
		return;

	stopVideoTest();
	videoPreview->setText(message);
}

void SettingsDialog::fillDevicesComboBox(QComboBox *cb, MediaDevice *dev, enum MediaDeviceStreamDirection dir)
{
		cb->clear();
		QList<MediaDeviceInfo> md = dev->Enumerate(dir);
		for (int i = 0; i < md.size(); i++)
		{
			QString item = QString(QLatin1String("%2\n(%1)"))
									.arg(md.at(i).device)
									.arg(md.at(i).humanReadable);

			// Cameras that can deliver a compressed stream themselves say so,
			// which is what the "prefer compressed formats" setting acts on.
			if (!md.at(i).compressedFormats.isEmpty())
				item += QString(QLatin1String(" [%1]")).arg(md.at(i).compressedFormats.join(QLatin1String(", ")));

			cb->addItem(item, md.at(i).device);
		}
}

void SettingsDialog::fillVideoDevices()
{
		VideoDevice vd;
		fillDevicesComboBox(videoDev, &vd, CAPTURE);
}

void SettingsDialog::fillAudioCaptureDevices()
{
	AudioDevice acd;
	fillDevicesComboBox(audioCaptureDev, &acd, CAPTURE);
}

void SettingsDialog::fillAudioPlaybackDevices()
{
	AudioDevice apd;
	fillDevicesComboBox(audioPlaybackDev, &apd, PLAYBACK);
}

void SettingsDialog::fillAudioRingDevices()
{
	AudioDevice ard;
	fillDevicesComboBox(audioRingDev, &ard, PLAYBACK);
}

void SettingsDialog::toggleIncomingConnections(bool checked)
{
	addr->setEnabled(checked);
	publicPort->setEnabled(checked);
	localPort->setEnabled(checked);
#ifdef HAVE_LIBMINIUPNPC
	cbEnableUPnP->setEnabled(checked);
#endif
}

void SettingsDialog::toggleMultipleLogins(bool checked)
{

}

void SettingsDialog::OK()
{
	storeSettings();
	done(1);
}

void SettingsDialog::Cancel()
{
	done(0);
}

void SettingsDialog::loadSettings()
{
	Settings st(config_dbname);

	//Direct c2c connections
	int c2c_state;

	c2c_state = st.loadValue("direct_c2c.state", 1).toInt();

	switch (c2c_state)
	{
	case 0:
		rbDisableDirectConns->setChecked(true);
		break;

	case 1:
		rbEnableOutgoingConns->setChecked(true);
		break;

	case 2:
		rbEnableIncomingConns->setChecked(true);
		break;

	default:
		break;
	}

#ifdef HAVE_LIBMINIUPNPC
	cbEnableUPnP->setChecked(st.loadValue("direct_c2c.upnp_enabled", 1).toBool());
#endif

	addr->lineEdit()->setText(st.loadValue("direct_c2c.public_addr", "autoconfigure").toString());

	if (addr->lineEdit()->text().contains(QString("autoconfigure")))
	{
		in_addr_t guess;
		struct in_addr in;

		guess = PICA_guess_listening_addr_ipv4();
		in.s_addr = guess;
		addr->lineEdit()->setText(QString("autoconfigured(%1)").arg(inet_ntoa(in)));

#ifdef HAVE_LIBMINIUPNPC
		if (cbEnableUPnP->isChecked() && PICA_is_reserved_addr_ipv4(guess))
		{
			int ret;
			char public_ip[64];
			ret = PICA_upnp_autoconfigure_ipv4(st.loadValue("direct_c2c.public_port", 2298).toInt(),
			                                   st.loadValue("direct_c2c.local_port", 2298).toInt(),
			                                   public_ip);

			if (ret)
			{
				addr->lineEdit()->setText(QString("autoconfigured(%1)").arg(public_ip));
			}
		}
#endif
	}

	publicPort->setValue(st.loadValue("direct_c2c.public_port", 2298).toInt());
	localPort->setValue(st.loadValue("direct_c2c.local_port", 2298).toInt());

	//multiple logins
	int mlpstate;
	mlpstate = c2c_state = st.loadValue("multiple_logins.state", 0).toInt();

	switch(mlpstate)
	{
	case PICA_MULTILOGIN_PROHIBIT:
		rbMLPProhibit->setChecked(true);
		break;

	case PICA_MULTILOGIN_REPLACE:
		rbMLPReplace->setChecked(true);
		break;

	case PICA_MULTILOGIN_ALLOW:
		rbMLPAllowMultiple->setChecked(true);
		break;

	default:
		break;
	}

	QString videoCapDevVal = st.loadValue("video.capture_device", QString()).toString();
	int videoDevItem = videoDev->findData(videoCapDevVal);
	if (videoDevItem >= 0)
		videoDev->setCurrentIndex(videoDevItem);

	cbPreferCompressed->setChecked(st.loadValue("video.prefer_compressed", 0).toBool());

	QString audioCapDevVal = st.loadValue("audio.capture_device", "default").toString();
	int audioCapDevItem = audioCaptureDev->findData(audioCapDevVal);
	if (audioCapDevItem >= 0)
		audioCaptureDev->setCurrentIndex(audioCapDevItem);

	QString audioPlaybackDevVal = st.loadValue("audio.playback_device", "default").toString();
	int audioPlaybackDevItem = audioPlaybackDev->findData(audioPlaybackDevVal);
	if (audioPlaybackDevItem >= 0)
		audioPlaybackDev->setCurrentIndex(audioPlaybackDevItem);

	QString audioRingDevVal = st.loadValue("audio.ring_device", "default").toString();
	int audioRingDevItem = audioRingDev->findData(audioRingDevVal);
	if (audioRingDevItem >= 0)
		audioRingDev->setCurrentIndex(audioRingDevItem);
}

void SettingsDialog::storeSettings()
{
	Settings st(config_dbname);

	//Direct c2c connections
	int c2c_state;

	if (rbDisableDirectConns->isChecked())
		c2c_state = 0;
	else if (rbEnableOutgoingConns->isChecked())
		c2c_state = 1;
	else if (rbEnableIncomingConns->isChecked())
		c2c_state = 2;

	st.storeValue("direct_c2c.state", QString::number(c2c_state));

#ifdef HAVE_LIBMINIUPNPC
	st.storeValue("direct_c2c.upnp_enabled", cbEnableUPnP->isChecked() ? "1" : "0");
#endif

	st.storeValue("direct_c2c.public_addr", addr->lineEdit()->text());
	st.storeValue("direct_c2c.public_port", QString::number(publicPort->value()));
	st.storeValue("direct_c2c.local_port", QString::number(localPort->value()));

	//multiple logins
	int mlpstate;

	if (rbMLPProhibit->isChecked())
		mlpstate = PICA_MULTILOGIN_PROHIBIT;
	else if (rbMLPReplace->isChecked())
		mlpstate = PICA_MULTILOGIN_REPLACE;
	else if (rbMLPAllowMultiple->isChecked())
		mlpstate = PICA_MULTILOGIN_ALLOW;

	st.storeValue("multiple_logins.state", QString::number(mlpstate));

	st.storeValue("video.capture_device", videoDev->itemData(videoDev->currentIndex()).toString());
	st.storeValue("video.prefer_compressed", cbPreferCompressed->isChecked() ? "1" : "0");
	st.storeValue("audio.capture_device", audioCaptureDev->itemData(audioCaptureDev->currentIndex()).toString());
	st.storeValue("audio.playback_device", audioPlaybackDev->itemData(audioPlaybackDev->currentIndex()).toString());
	st.storeValue("audio.ring_device", audioRingDev->itemData(audioRingDev->currentIndex()).toString());
}
