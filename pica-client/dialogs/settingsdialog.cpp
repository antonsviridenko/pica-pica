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
#include "../callsettings.h"
#include "../../PICA_netconf.h"
#include "../../PICA_proto.h"

#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDebug>
#include <QVariant>
#include <QListWidget>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QSize>
#include <QTabWidget>
#include <QTimer>

// Audio pipeline test: how long the microphone is recorded for. The codec and
// the rate come from the "Call Settings" tab, so the test runs the same path a
// call would - see toggleAudioTest().
static const int kAudioTestRecordMs = 5000;

// One encoded packet covers this much audio (the Opus encoder produces
// 20 ms frames at 48 kHz), so feeding one packet per this many milliseconds
// replays the recording at its natural rate.
static const int kAudioTestPacketMs = 20;

// How long to wait for the microphone to produce its first packet before
// giving up on it. Opening a capture device is not instant - on Windows,
// COM plus WASAPI endpoint activation can take the better part of a second -
// so the recording window below is only started once audio is actually
// arriving, and this is the separate limit on getting that far at all.
static const int kAudioTestStartTimeoutMs = 4000;

// Grace period after the last packet has been handed over, for the jitter
// buffer and the sound card's own buffer to drain before closing the device.
static const int kAudioTestDrainMs = 500;

// Sizes and frame rates offered when the camera will not say what it supports
// - a platform where they cannot be queried, or a camera that will not answer.
// The camera is free to refuse any of them, the same way it may refuse
// anything else it was asked for; VideoDevice::Capture() loosens the request
// rather than failing.
static const struct
{
	int width;
	int height;
} kFallbackResolutions[] =
{
	{ 1920, 1080 }, { 1280, 720 }, { 800, 600 }, { 640, 480 },
	{ 640, 360 }, { 352, 288 }, { 320, 240 }, { 160, 120 }
};

static const int kFallbackFrameRates[] = { 30, 25, 24, 20, 15, 10, 5 };

// Range the bitrate spin boxes accept, in kbit/s. Wide enough not to argue
// with anyone: the low end is where a codec still produces something, the high
// end well past what a call over a home connection can carry.
static const int kMinVideoBitrateKbps = 50;
static const int kMaxVideoBitrateKbps = 20000;
static const int kMinAudioBitrateKbps = 6;
static const int kMaxAudioBitrateKbps = 510;

SettingsDialog::SettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	// The video tab's format lists follow the camera selection, and
	// fillVideoDevices() below changes that before they have been built - so
	// they have to be readable as "not there yet" from the moment anything can
	// reach them.
	videoRes = nullptr;
	videoFps = nullptr;

	QVBoxLayout *settingsLayout = new QVBoxLayout();

	QTabWidget *tabW = new QTabWidget(this);
	QWidget* directc2ctab = new QWidget(0);
	QWidget* soundstab = new QWidget(0);
	QWidget* multilogintab = new QWidget(0);
	QWidget* audiodevtab = new QWidget(0);
	QWidget* videodevtab = new QWidget(0);
	QWidget* calltab = new QWidget(0);

	QVBoxLayout *directc2cLayout = new QVBoxLayout();
	QVBoxLayout *multiloginlayout = new QVBoxLayout();
	QVBoxLayout *audiodevlayout = new QVBoxLayout();
	QVBoxLayout *videodevLayout = new QVBoxLayout();
	QVBoxLayout *callLayout = new QVBoxLayout();

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
	audioDriver = nullptr;
#ifdef Q_OS_LINUX
	QLabel *lbAudioDriver = new QLabel(tr("Audio system"));

	// ALSA is always there; PulseAudio is what a desktop session normally
	// runs (PipeWire included, through its PulseAudio interface) and is the
	// one that can hand us a device with the echo already cancelled.
	audioDriver = new QComboBox(this);
	audioDriver->addItem(tr("ALSA"), QStringLiteral("alsa"));
	audioDriver->addItem(tr("PulseAudio / PipeWire"), QStringLiteral("pulse"));

	// Set before the device lists are filled, since Enumerate() asks it which
	// driver's names to go looking for.
	{
		Settings drvst(config_dbname);
		QString drv = drvst.loadValue("audio.driver", "alsa").toString();
		AudioDevice::SetLinuxDriverName(drv);
		int drvItem = audioDriver->findData(drv);
		if (drvItem >= 0)
			audioDriver->setCurrentIndex(drvItem);
	}

	connect(audioDriver, SIGNAL(currentIndexChanged(int)), this, SLOT(audioDriverChanged(int)));
#endif

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

	// Echo cancellation: removing the sound of the other person coming back
	// out of the microphone, which is what makes them hear themselves when
	// the call is on speakers. Exactly one of the three - two cancellers in
	// series is worse than either alone, since the second one is left
	// chasing a reference that is no longer in the signal.
	QGroupBox *gbEchoCancel = new QGroupBox(tr("Audio echo cancellation"), this);
	QVBoxLayout *echoCancelLayout = new QVBoxLayout();

	// Parented to the group box, not the dialog: QRadioButton's automatic
	// exclusivity groups by parent widget, and these three must not join the
	// direct connection or multiple login buttons.
	rbEchoCancelOwn = new QRadioButton(tr("Use own echo cancellation"), gbEchoCancel);
	rbEchoCancelOwn->setToolTip(tr("Cancels the echo inside Pica Pica, using WebRTC's AEC3. "
	                               "Works with any microphone and any sound system, and is the "
	                               "only option where the platform provides nothing."));

	rbEchoCancelPlatform = new QRadioButton(tr("Use platform provided echo cancellation"), gbEchoCancel);

	rbEchoCancelNone = new QRadioButton(tr("Disable echo cancellation"), gbEchoCancel);
	rbEchoCancelNone->setToolTip(tr("Nothing is removed from the microphone signal. Right with a "
	                                "headset, where there is no echo to cancel, and worth trying "
	                                "if cancellation is cutting into your speech."));

	echoCancelLayout->addWidget(rbEchoCancelOwn);
	echoCancelLayout->addWidget(rbEchoCancelPlatform);
	echoCancelLayout->addWidget(rbEchoCancelNone);
	gbEchoCancel->setLayout(echoCancelLayout);

	// Fills in the platform option's tooltip and greys it out where there is
	// no such thing. loadSettings() runs afterwards and is what actually
	// picks one of the three.
	updateEchoCancellationChoices();

	btRingTest = new QPushButton(tr("Test ring 🔔"), this);
	connect(btRingTest, SIGNAL(clicked()), this, SLOT(toggleRingTest()));

	ringTestStatus = new QLabel(this);
	ringTestStatus->setAlignment(Qt::AlignCenter);

	// The microphone test covers the capture and playback devices, so it sits
	// with them, above the unrelated ring device below.
#ifdef Q_OS_LINUX
	audiodevlayout->addWidget(lbAudioDriver);
	audiodevlayout->addWidget(audioDriver);
#endif
	audiodevlayout->addWidget(lbAudioCaptureDev);
	audiodevlayout->addWidget(audioCaptureDev);
	audiodevlayout->addWidget(lbAudioPlaybackDev);
	audiodevlayout->addWidget(audioPlaybackDev);
	audiodevlayout->addWidget(gbEchoCancel);
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
	// A coarse timer is no use at this interval. Windows' default system
	// timer granularity is 15.6 ms, so a coarse 20 ms timer really fires
	// about every 31 ms; PreciseTimer asks for a multimedia timer instead.
	// audioTestFeedPacket() does not rely on this being exact, but the closer
	// it is the smoother the playback.
	audioTestPlaybackTimer->setTimerType(Qt::PreciseTimer);
	connect(audioTestPlaybackTimer, SIGNAL(timeout()), this, SLOT(audioTestFeedPacket()));

	audioTestDrainTimer = new QTimer(this);
	audioTestDrainTimer->setSingleShot(true);
	connect(audioTestDrainTimer, SIGNAL(timeout()), this, SLOT(audioTestPlaybackFinished()));

	testMic = new AudioDevice();
	testMic->moveToThread(&testMicThread);
	connect(testMic, SIGNAL(packetReady(QByteArray)), this, SLOT(audioTestPacket(QByteArray)));
	connect(testMic, SIGNAL(errorOccurred(QString)), this, SLOT(audioTestError(QString)));
	connect(testMic, SIGNAL(deviceFormatInUse(QString)), this, SLOT(audioTestPath(QString)));
	testMicThread.start();

	testSpeaker = new AudioDevice();
	testSpeaker->moveToThread(&testSpeakerThread);
	connect(testSpeaker, SIGNAL(errorOccurred(QString)), this, SLOT(audioTestError(QString)));
	connect(testSpeaker, SIGNAL(deviceFormatInUse(QString)), this, SLOT(audioTestPath(QString)));
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

	QLabel *lbVideoRes = new QLabel(tr("Resolution"), this);
	videoRes = new QComboBox(this);
	videoRes->setToolTip(tr("Picture sizes the selected camera offers. A size the camera can "
	                        "deliver already compressed lists the formats in brackets; those "
	                        "are the ones the \"prefer compressed formats\" setting below can "
	                        "forward untouched, without decoding and re-encoding them here."));

	QLabel *lbVideoFps = new QLabel(tr("Frame rate"), this);
	videoFps = new QComboBox(this);
	videoFps->setToolTip(tr("Frame rates the camera offers at the selected size."));

	// Filled from the camera the device list above has selected, so the two
	// have to follow it - and each other.
	fillVideoResolutions();
	connect(videoDev, SIGNAL(currentIndexChanged(int)), this, SLOT(fillVideoResolutions()));
	connect(videoRes, SIGNAL(currentIndexChanged(int)), this, SLOT(fillVideoFrameRates()));

	QHBoxLayout *videoFormatLayout = new QHBoxLayout();
	videoFormatLayout->addWidget(lbVideoRes);
	videoFormatLayout->addWidget(videoRes, 1);
	videoFormatLayout->addWidget(lbVideoFps);
	videoFormatLayout->addWidget(videoFps);

	cbPreferCompressed = new QCheckBox(tr("Prefer compressed formats if provided by the camera"), this);

#ifdef HAVE_VAAPI
	cbVaapiEncoding = new QCheckBox(tr("enable VAAPI-accelerated hardware encoding"), this);
	cbVaapiDecoding = new QCheckBox(tr("enable VAAPI-accelerated hardware decoding"), this);
	cbVaapiRendering = new QCheckBox(tr("enable VAAPI accelerated hardware rendering"), this);
	// Drawing a VA surface means having decoded into one first, so rendering
	// brings decoding along with it whether or not it was asked for.
	cbVaapiRendering->setToolTip(tr("Requires hardware decoding, which is enabled along with it"));
#endif

	btVideoTest = new QPushButton(tr("Test 📷"), this);
	connect(btVideoTest, SIGNAL(clicked()), this, SLOT(toggleVideoTest()));

	videoPreview = new QLabel(this);
	videoPreview->setAlignment(Qt::AlignCenter);
	videoPreview->setMinimumSize(320, 240);
	videoPreview->setFrameShape(QFrame::StyledPanel);

	videoTestStatus = new QLabel(this);
	videoTestStatus->setAlignment(Qt::AlignCenter);

#ifdef HAVE_VAAPI
	videoPreviewGpu = VaapiRenderWidget::create(this);
	videoPreviewGpu->widget()->setMinimumSize(320, 240);
	videoPreviewGpu->widget()->hide();
	connect(videoPreviewGpu->widget(), SIGNAL(renderingFailed(QString)), this, SLOT(videoTestRenderFailed(QString)));
#endif

	videodevLayout->addWidget(videoDev);
	videodevLayout->addWidget(videoDevRefresh);
	videodevLayout->addLayout(videoFormatLayout);
	videodevLayout->addWidget(cbPreferCompressed);
#ifdef HAVE_VAAPI
	videodevLayout->addWidget(cbVaapiEncoding);
	videodevLayout->addWidget(cbVaapiDecoding);
	videodevLayout->addWidget(cbVaapiRendering);
#endif
	videodevLayout->addWidget(btVideoTest);
	videodevLayout->addWidget(videoPreview);
#ifdef HAVE_VAAPI
	videodevLayout->addWidget(videoPreviewGpu->widget());
#endif
	videodevLayout->addWidget(videoTestStatus);
	videodevLayout->addStretch(1);

	videodevtab->setLayout(videodevLayout);

	// Capture+encode and decode ends of the test pipeline, each blocking its
	// own thread while running, exactly as they do during a call. They stay
	// idle until the Test button is pressed.
	videoTestRunning = false;
	testVideoSeq = 0;

	testCam = new VideoDevice();
	testCam->moveToThread(&testCamThread);
	connect(testCam, SIGNAL(captureStarted(QString,int,int)), this, SLOT(videoTestCaptureStarted(QString,int,int)));
	connect(testCam, SIGNAL(packetReady(QByteArray,bool)), this, SLOT(videoTestFragment(QByteArray,bool)));
	connect(testCam, SIGNAL(errorOccurred(QString)), this, SLOT(videoTestError(QString)));
	connect(testCam, SIGNAL(accelerationInUse(QString)), this, SLOT(videoTestPath(QString)));
	testCamThread.start();

	testDecoder = new VideoDevice();
	testDecoder->moveToThread(&testDecoderThread);
	connect(testDecoder, SIGNAL(frameReady(QImage)), this, SLOT(videoTestFrame(QImage)));
	connect(testDecoder, SIGNAL(errorOccurred(QString)), this, SLOT(videoTestError(QString)));
	connect(testDecoder, SIGNAL(accelerationInUse(QString)), this, SLOT(videoTestPath(QString)));
#ifdef HAVE_VAAPI
	connect(testDecoder, SIGNAL(hwFrameReady(AVFramePtr)), this, SLOT(videoTestHwFrame(AVFramePtr)));
#endif
	testDecoderThread.start();

//Call Settings
	// What a call encodes with. The codec names stored here are FFmpeg names,
	// which is also how the 0x74 and 0x75 messages name them - see
	// callsettings.h.
	callVideoCodec = new QComboBox(this);

	for (int i = 0; i < kCallVideoCodecCount; i++)
		callVideoCodec->addItem(QLatin1String(kCallVideoCodecs[i].displayName),
		                        QLatin1String(kCallVideoCodecs[i].name));

	callVideoCodec->setToolTip(tr("What outgoing video is encoded with. The peer is told which "
	                              "codec is in use, so it does not have to be one it was "
	                              "configured for.\n\n"
	                              "This applies to video encoded here. With \"prefer compressed "
	                              "formats\" switched on and a camera that delivers a compressed "
	                              "stream itself, that stream is forwarded as it comes and the "
	                              "camera's format is what the peer receives - this setting then "
	                              "only decides which of the camera's formats is preferred."));

	callAudioCodec = new QComboBox(this);

	for (int i = 0; i < kCallAudioCodecCount; i++)
		callAudioCodec->addItem(QLatin1String(kCallAudioCodecs[i].displayName),
		                        QLatin1String(kCallAudioCodecs[i].name));

	callAudioCodec->setToolTip(tr("What outgoing audio is encoded with. Opus is the one to want "
	                              "for a voice call; G.722 and G.711 A-law are the telephony "
	                              "codecs, fixed at 64 kbit/s and there for compatibility."));

	callVideoBitrate = new QSpinBox(this);
	callVideoBitrate->setRange(kMinVideoBitrateKbps, kMaxVideoBitrateKbps);
	callVideoBitrate->setSuffix(tr(" kbps"));
	callVideoBitrate->setValue(kDefaultVideoBitrateKbps);

	callAudioBitrate = new QSpinBox(this);
	callAudioBitrate->setRange(kMinAudioBitrateKbps, kMaxAudioBitrateKbps);
	callAudioBitrate->setSuffix(tr(" kbps"));
	callAudioBitrate->setValue(kDefaultAudioBitrateKbps);
	audioBitrateKbps = kDefaultAudioBitrateKbps;

	connect(callAudioCodec, SIGNAL(currentIndexChanged(int)), this, SLOT(callAudioCodecChanged()));

	QFormLayout *callFormLayout = new QFormLayout();
	callFormLayout->addRow(tr("Video Codec"), callVideoCodec);
	callFormLayout->addRow(tr("Video Bitrate"), callVideoBitrate);
	callFormLayout->addRow(tr("Audio Codec"), callAudioCodec);
	callFormLayout->addRow(tr("Audio Bitrate"), callAudioBitrate);

	// The other half of what a call is started with lives on the video tab,
	// next to the camera that has to deliver it; say so rather than leaving
	// this tab looking like the whole story.
	QLabel *lbCallHint = new QLabel(tr("The picture size and frame rate a call is started with are "
	                                   "on the Video Devices tab, next to the camera that has to "
	                                   "deliver them."), this);
	lbCallHint->setWordWrap(true);

	callLayout->addLayout(callFormLayout);
	callLayout->addWidget(lbCallHint);
	callLayout->addStretch(1);

	calltab->setLayout(callLayout);

	tabW->addTab(directc2ctab, tr("Direct Connections"));
	tabW->addTab(multilogintab, tr("Multiple logins"));
	tabW->addTab(audiodevtab, tr("Audio Devices"));
	tabW->addTab(videodevtab, tr("Video Devices"));
	tabW->addTab(calltab, tr("Call Settings"));
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
	audioTestPathReport.clear();
	audioTestState = AudioTestRecording;
	btAudioTest->setText(tr("Stop ⏹"));
	audioTestStatus->setText(tr("Recording, say something..."));

	// The codec selected on the "Call Settings" tab, at the rate a call with
	// it would run at - same as the video test, this exercises what the
	// current selections do rather than a fixed pipeline of its own.
	const QString codec = callAudioCodec->itemData(callAudioCodec->currentIndex()).toString();
	const int rate = callAudioSampleRate(codec);
	const int bitrate = callAudioBitrateAdjustable(codec) ? callAudioBitrate->value() * 1000 : 0;

	QMetaObject::invokeMethod(testMic, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, dev), Q_ARG(QString, codec),
	                           Q_ARG(int, rate), Q_ARG(int, bitrate));
	QMetaObject::invokeMethod(testMic, "Capture", Qt::QueuedConnection);

	// Only a watchdog for the device failing to open at all. The recording
	// window proper starts in audioTestPacket(), when the first packet shows
	// up - otherwise the device's startup time comes out of the five seconds
	// and the user gets a noticeably short recording.
	audioTestRecordTimer->start(kAudioTestStartTimeoutMs);
}

void SettingsDialog::audioTestPacket(QByteArray data)
{
	// Packets still in flight from the microphone thread after the recording
	// phase ended are not part of the recording.
	if (audioTestState != AudioTestRecording)
		return;

	// First packet: the microphone is live, so start the recording window now
	// rather than when the device was merely asked to open.
	if (audioTestPackets.isEmpty())
		audioTestRecordTimer->start(kAudioTestRecordMs);

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

	// The packet count is worth showing, not just decoration. Capture emits
	// one packet per kAudioTestPacketMs of audio it actually received, so a
	// count well under kAudioTestRecordMs/kAudioTestPacketMs means the
	// recording lost samples on the way in, and anything wrong with the sound
	// is a capture problem. A full count with bad sound means the samples all
	// arrived and something downstream is mangling them.
	QString summary = tr("Playing back %1 of %2 packets (%3 s)...")
	                  .arg(audioTestPackets.size())
	                  .arg(kAudioTestRecordMs / kAudioTestPacketMs)
	                  .arg(audioTestPackets.size() * kAudioTestPacketMs / 1000.0, 0, 'f', 1);

	if (!audioTestPathReport.isEmpty())
		summary += QLatin1String("\n") + audioTestPathReport;

	audioTestStatus->setText(summary);

	// Decoded with what it was encoded with, exactly as the receiving end of a
	// call decodes with whatever the peer announced.
	const QString codec = callAudioCodec->itemData(callAudioCodec->currentIndex()).toString();

	QMetaObject::invokeMethod(testSpeaker, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, dev), Q_ARG(QString, codec),
	                           Q_ARG(int, callAudioSampleRate(codec)));
	QMetaObject::invokeMethod(testSpeaker, "Play", Qt::QueuedConnection);

	// The recording has to be handed over at the rate it plays at, not all at
	// once: AudioDevice's jitter buffer holds only a few packets and drops
	// the oldest when full, so dumping the whole recording into it would
	// discard everything but the tail.
	audioTestPlaybackClock.start();
	audioTestPlaybackTimer->start(kAudioTestPacketMs);
}

void SettingsDialog::audioTestFeedPacket()
{
	if (audioTestState != AudioTestPlaying)
		return;

	// Pace against the clock, not against the number of times this has been
	// called. A QTimer asked for 20 ms does not necessarily deliver every
	// 20 ms - on Windows the system timer granularity is 15.6 ms, so even a
	// precise timer can run late - and handing over one packet per tick
	// regardless would play the recording back at whatever rate the timer
	// happened to run at. Late ticks stretched it out and left the sound card
	// with nothing to play in between, which sounded like the recording was
	// both slowed down and chopped up.
	//
	// The real call path has no equivalent problem: there the packets are
	// paced by the peer sending them, not by a timer here.
	const qint64 due = audioTestPlaybackClock.elapsed() / kAudioTestPacketMs + 1;

	while (audioTestPlaybackPos < audioTestPackets.size() &&
	       (qint64)audioTestPlaybackPos < due)
	{
		// Direct call, for the same reason as in the call controller. Nothing
		// here loses or reorders packets, so the sequence numbers are simply
		// the order they were recorded in.
		testSpeaker->enqueuePacket((quint16)audioTestPlaybackPos,
		                           audioTestPackets.at(audioTestPlaybackPos));
		audioTestPlaybackPos++;
	}

	if (audioTestPlaybackPos >= audioTestPackets.size())
	{
		audioTestPlaybackTimer->stop();
		// Let what is already queued, plus the sound card's own buffer, play
		// out before tearing the playback device down.
		audioTestDrainTimer->start(kAudioTestDrainMs);
	}
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

void SettingsDialog::audioTestPath(QString description)
{
	if (description.isEmpty())
		return;

	if (!audioTestPathReport.isEmpty())
		audioTestPathReport += QLatin1String("\n");
	audioTestPathReport += description;
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
	testVideoSeq = 0;
	videoTestRunning = true;
	btVideoTest->setText(tr("Stop ⏹"));
	videoPreview->setText(tr("Starting..."));

	bool vaapiEncoding = false;
#ifdef HAVE_VAAPI
	vaapiEncoding = cbVaapiEncoding->isChecked();
#endif

	videoTestPathReport.clear();

	// The settings as they stand in the dialog, not as they were last stored:
	// the point of the test is to see what the current selections do before
	// committing to them.
	QMetaObject::invokeMethod(testCam, "configureCapture", Qt::QueuedConnection,
	                           Q_ARG(QString, dev),
	                           Q_ARG(int, selectedVideoWidth()), Q_ARG(int, selectedVideoHeight()),
	                           Q_ARG(int, selectedVideoFrameRate()),
	                           Q_ARG(bool, cbPreferCompressed->isChecked()),
	                           Q_ARG(QString, callVideoCodec->itemData(callVideoCodec->currentIndex()).toString()),
	                           Q_ARG(int, callVideoBitrate->value() * 1000),
	                           Q_ARG(bool, vaapiEncoding));
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
	videoTestFormat = tr("%1 %2x%3").arg(codec).arg(width).arg(height);
	videoPreview->setText(tr("Capturing %1...").arg(videoTestFormat));

	bool vaapiDecoding = false;
	bool vaapiRendering = false;
#ifdef HAVE_VAAPI
	vaapiDecoding = cbVaapiDecoding->isChecked();
	// Asking for frames to be kept in GPU memory once it is known that they
	// cannot be drawn from there would only mean starting each test with a
	// blank preview until it failed again.
	vaapiRendering = cbVaapiRendering->isChecked() && !VaapiRenderWidget::renderingKnownBroken();
#endif

	QMetaObject::invokeMethod(testDecoder, "configurePlayback", Qt::QueuedConnection,
	                           Q_ARG(QString, codec),
	                           Q_ARG(int, width), Q_ARG(int, height),
	                           Q_ARG(bool, vaapiDecoding), Q_ARG(bool, vaapiRendering));
	QMetaObject::invokeMethod(testDecoder, "Play", Qt::QueuedConnection);
}

void SettingsDialog::videoTestPath(QString description)
{
	if (!videoTestRunning)
		return;

	// Capture and playback each report once; both lines are kept so the whole
	// path is visible, since either end can quietly fall back to software.
	if (!videoTestPathReport.isEmpty())
		videoTestPathReport += QLatin1String(", ");
	videoTestPathReport += description;
}

void SettingsDialog::stopVideoTest()
{
	// Direct calls: both devices are blocked inside their capture/decode
	// loops, so a queued call would never reach them (see the comments in
	// AudioVideoCallController::stopAudioPipeline()).
	testCam->Close();
	testDecoder->Close();

	testAssembler.reset();
	testVideoSeq = 0;
	videoTestRunning = false;
	videoTestFormat.clear();
	videoTestPathReport.clear();
	btVideoTest->setText(tr("Test 📷"));
	videoPreview->clear();
	videoTestStatus->clear();
#ifdef HAVE_VAAPI
	videoPreviewGpu->clearFrame();
	videoPreviewGpu->widget()->hide();
	videoPreview->show();
#endif
}

void SettingsDialog::videoTestFragment(QByteArray data, bool is_last_fragment)
{
	if (!videoTestRunning)
		return;

	// The encoder emits fragments exactly as they would go onto the wire, so
	// feed them through the same assembler the receiving end of a call uses.
	// The sequence number is rebuilt here the way send_video_packet() builds
	// it - the counter has to be kept as well, since the assembler uses it to
	// tell a fragment that follows the previous one from one that comes after
	// a loss. The timestamp is constant, as nothing here can lose a fragment
	// and only frame boundaries matter.
	quint16 seq = testVideoSeq & VideoFrameAssembler::SeqNumMask;

	testVideoSeq = (testVideoSeq + 1) & VideoFrameAssembler::SeqNumMask;

	if (is_last_fragment)
		seq |= VideoFrameAssembler::LastFragmentFlag;

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

	// The preview itself is showing pixels now, so what format came off the
	// camera and which of the GPU or CPU did the work goes underneath it.
	videoTestStatus->setText(videoTestPathReport.isEmpty()
	                         ? videoTestFormat
	                         : tr("%1 - %2").arg(videoTestFormat, videoTestPathReport));

	videoPreview->setPixmap(QPixmap::fromImage(frame).scaled(videoPreview->size(),
	                                                         Qt::KeepAspectRatio,
	                                                         Qt::SmoothTransformation));
}

#ifdef HAVE_VAAPI
void SettingsDialog::videoTestHwFrame(AVFramePtr frame)
{
	if (!videoTestRunning)
		return;

	// The widget has given up on drawing; leave the preview area with the
	// label and get the decoder to produce frames it can show.
	if (!videoPreviewGpu->isWorking())
	{
		testDecoder->disableHardwareRendering();
		return;
	}

	// Frames arriving here never went through system memory, so the label has
	// nothing to show - the GL widget takes over the preview area.
	if (videoPreviewGpu->widget()->isHidden())
	{
		videoPreview->hide();
		videoPreviewGpu->widget()->show();
	}

	videoTestStatus->setText(videoTestPathReport.isEmpty()
	                         ? videoTestFormat
	                         : tr("%1 - %2, GPU rendering").arg(videoTestFormat, videoTestPathReport));

	videoPreviewGpu->setFrame(frame);
}

void SettingsDialog::videoTestRenderFailed(QString message)
{
	// Drawing straight from GPU memory did not work out. The decoder is still
	// running, so leave the test going rather than killing it: ask it to bring
	// frames back into system memory, which the label can show. The reason
	// stands in the label until the first of those frames replaces it.
	qWarning() << message;

	videoPreviewGpu->widget()->hide();
	videoPreview->show();
	videoPreview->setText(message);

	// Direct call, for the same reason as in videoTestFragment().
	testDecoder->disableHardwareRendering();
}
#endif

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

			// A camera's compressed formats are not listed here but in the
			// resolution list, which is where they actually mean something: a
			// camera offers them at some of its sizes and not at others, so
			// naming them against the camera as a whole said less than it
			// appeared to.

			cb->addItem(item, md.at(i).device);
		}
}

void SettingsDialog::fillVideoDevices()
{
		VideoDevice vd;

		// Refilling walks the current index through -1 and back, and each step
		// of that would otherwise ask a camera what it can do - which is not
		// free, on Windows it means instantiating the capture filter. So the
		// format lists below are rebuilt once, at the end.
		const QString previous = videoDev->itemData(videoDev->currentIndex()).toString();

		videoDev->blockSignals(true);
		fillDevicesComboBox(videoDev, &vd, CAPTURE);

		// Refresh is for picking up a camera that was plugged in, not for
		// moving the selection off the one already chosen.
		int item = videoDev->findData(previous);
		if (item >= 0)
			videoDev->setCurrentIndex(item);

		videoDev->blockSignals(false);

		fillVideoResolutions();
}

void SettingsDialog::fillVideoResolutions()
{
	// Nothing to fill into yet while the constructor is still building the
	// tab - fillVideoDevices() runs before these exist.
	if (!videoRes || !videoFps)
		return;

	const QString dev = videoDev->itemData(videoDev->currentIndex()).toString();

	videoFormats = dev.isEmpty() ? QList<VideoCaptureFormat>()
	                             : VideoDevice::CaptureFormats(dev);

	// Refilling moves the current index about; the frame rate list is rebuilt
	// once at the end rather than on every one of those intermediate changes.
	videoRes->blockSignals(true);
	videoRes->clear();

	if (videoFormats.isEmpty())
	{
		// Either the camera would not say or this platform cannot ask. Offer
		// the common sizes rather than an empty list - a camera that cannot
		// produce one of them is handled the same way as any other request it
		// will not honour, see VideoDevice::Capture().
		for (unsigned int i = 0; i < sizeof(kFallbackResolutions) / sizeof(kFallbackResolutions[0]); i++)
		{
			const int w = kFallbackResolutions[i].width;
			const int h = kFallbackResolutions[i].height;

			videoRes->addItem(QString(QLatin1String("%1x%2")).arg(w).arg(h), QSize(w, h));
		}
	}
	else
	{
		for (int i = 0; i < videoFormats.size(); i++)
		{
			const VideoCaptureFormat &f = videoFormats.at(i);
			QString item = QString(QLatin1String("%1x%2")).arg(f.width).arg(f.height);

			// The formats this camera can hand over ready-made at this size,
			// which is what "prefer compressed formats" below acts on.
			if (!f.compressedFormats.isEmpty())
				item += QString(QLatin1String(" [%1]")).arg(f.compressedFormats.join(QLatin1String(", ")));

			videoRes->addItem(item, QSize(f.width, f.height));
		}
	}

	// Keep the configured size selected across a device change where the new
	// camera offers it too, rather than silently moving the setting to
	// whatever happens to be first in the new list.
	Settings st(config_dbname);
	QSize wanted(st.loadValue("video.capture_width", kDefaultCaptureWidth).toInt(),
	             st.loadValue("video.capture_height", kDefaultCaptureHeight).toInt());

	int item = videoRes->findData(wanted);

	if (item < 0)
		item = videoRes->findData(QSize(kDefaultCaptureWidth, kDefaultCaptureHeight));

	if (item >= 0)
		videoRes->setCurrentIndex(item);

	videoRes->blockSignals(false);

	fillVideoFrameRates();
}

void SettingsDialog::fillVideoFrameRates()
{
	if (!videoFps)
		return;

	const QSize size = videoRes->itemData(videoRes->currentIndex()).toSize();
	QList<int> rates;

	for (int i = 0; i < videoFormats.size(); i++)
	{
		if (videoFormats.at(i).width == size.width() && videoFormats.at(i).height == size.height())
		{
			rates = videoFormats.at(i).frameRates;
			break;
		}
	}

	videoFps->blockSignals(true);
	videoFps->clear();

	if (rates.isEmpty())
	{
		// Same reasoning as for the sizes: a camera that named its sizes but
		// not its rates, or one we could not ask at all.
		for (unsigned int i = 0; i < sizeof(kFallbackFrameRates) / sizeof(kFallbackFrameRates[0]); i++)
			rates << kFallbackFrameRates[i];
	}

	for (int i = 0; i < rates.size(); i++)
		videoFps->addItem(tr("%1 fps").arg(rates.at(i)), rates.at(i));

	Settings st(config_dbname);
	int wanted = st.loadValue("video.capture_framerate", kDefaultCaptureFrameRate).toInt();

	int item = videoFps->findData(wanted);

	if (item < 0)
		item = videoFps->findData(kDefaultCaptureFrameRate);

	// Neither the configured rate nor the default is on offer at this size -
	// take the highest the camera does offer, which is the first in the list.
	if (item < 0 && videoFps->count() > 0)
		item = 0;

	if (item >= 0)
		videoFps->setCurrentIndex(item);

	videoFps->blockSignals(false);
}

int SettingsDialog::selectedVideoWidth() const
{
	const QSize size = videoRes->itemData(videoRes->currentIndex()).toSize();

	return size.isValid() ? size.width() : kDefaultCaptureWidth;
}

int SettingsDialog::selectedVideoHeight() const
{
	const QSize size = videoRes->itemData(videoRes->currentIndex()).toSize();

	return size.isValid() ? size.height() : kDefaultCaptureHeight;
}

int SettingsDialog::selectedVideoFrameRate() const
{
	bool ok = false;
	const int fps = videoFps->itemData(videoFps->currentIndex()).toInt(&ok);

	return (ok && fps > 0) ? fps : kDefaultCaptureFrameRate;
}

void SettingsDialog::callAudioCodecChanged()
{
	const QString codec = callAudioCodec->itemData(callAudioCodec->currentIndex()).toString();

	if (callAudioBitrateAdjustable(codec))
	{
		// Coming back from a fixed rate codec, whose rate is showing in the
		// box; the user's own choice is in audioBitrateKbps.
		if (!callAudioBitrate->isEnabled())
		{
			callAudioBitrate->setEnabled(true);
			callAudioBitrate->setValue(audioBitrateKbps);
			callAudioBitrate->setToolTip(QString());
		}

		return;
	}

	if (callAudioBitrate->isEnabled())
		audioBitrateKbps = callAudioBitrate->value();

	// Show what the codec will actually run at rather than leaving a number
	// behind that has nothing to do with it.
	callAudioBitrate->setEnabled(false);
	callAudioBitrate->setValue(callAudioFixedBitrateKbps(codec));
	callAudioBitrate->setToolTip(tr("This codec codes a fixed number of bits per sample, so its "
	                                "bitrate follows from the sample rate and cannot be chosen."));
}

void SettingsDialog::audioDriverChanged(int index)
{
	Q_UNUSED(index)

	if (!audioDriver)
		return;

	// Take effect straight away rather than on OK, so the device lists below
	// show the right names and the Test button uses them.
	AudioDevice::SetLinuxDriverName(audioDriver->itemData(audioDriver->currentIndex()).toString());

	fillAudioCaptureDevices();
	fillAudioPlaybackDevices();
	fillAudioRingDevices();

	// ALSA has no echo cancellation of its own and PulseAudio does, so the
	// platform option comes and goes with this choice.
	updateEchoCancellationChoices();
}

void SettingsDialog::updateEchoCancellationChoices()
{
	// PlatformDriverName() reads the driver SetLinuxDriverName() published,
	// which audioDriverChanged() has already updated by the time it calls
	// this - so the answer follows the combo box rather than the saved
	// setting.
	const QString driver = AudioDevice::PlatformDriverName(CAPTURE);
	const bool available = AudioDevice::PlatformEchoCancellationAvailable(driver);

	rbEchoCancelPlatform->setEnabled(available);

	if (available)
	{
		rbEchoCancelPlatform->setToolTip(tr("Lets the operating system or sound server cancel the "
		                                    "echo instead. Where it exists this is the better "
		                                    "one: it sits below Pica Pica and can see the real "
		                                    "speaker signal and the real clock.\n\n"
		                                    "With PulseAudio or PipeWire it also means picking an "
		                                    "echo cancelling source as the microphone above - a "
		                                    "plain input device is not processed."));
	}
	else
	{
		rbEchoCancelPlatform->setToolTip(tr("Not available: the audio system in use does not "
		                                    "cancel echo for us."));

		// A disabled button that is still the checked one would leave the
		// group showing a choice that is not in effect.
		if (rbEchoCancelPlatform->isChecked())
			rbEchoCancelOwn->setChecked(true);
	}
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

	// The resolution and frame rate lists pick the stored size and rate out of
	// what the selected camera offers as they are filled, which the line above
	// has just triggered where it changed the camera - see
	// fillVideoResolutions().

	cbPreferCompressed->setChecked(st.loadValue("video.prefer_compressed", 0).toBool());

	// Call encoding parameters. Read through CallSettings rather than from st
	// directly, so that the dialog and a call that is started without ever
	// opening it agree on what "not configured" means.
	const CallSettings cs = CallSettings::load();

	int callVideoCodecItem = callVideoCodec->findData(cs.videoCodec);
	if (callVideoCodecItem >= 0)
		callVideoCodec->setCurrentIndex(callVideoCodecItem);

	callVideoBitrate->setValue(qBound(kMinVideoBitrateKbps, cs.videoBitrateKbps, kMaxVideoBitrateKbps));

	// Before the codec is selected: selecting one whose bitrate is fixed puts
	// that codec's rate in the box and keeps this as the value to come back
	// to, see callAudioCodecChanged().
	audioBitrateKbps = qBound(kMinAudioBitrateKbps, cs.audioBitrateKbps, kMaxAudioBitrateKbps);
	callAudioBitrate->setValue(audioBitrateKbps);

	int callAudioCodecItem = callAudioCodec->findData(cs.audioCodec);
	if (callAudioCodecItem >= 0)
		callAudioCodec->setCurrentIndex(callAudioCodecItem);

	// Explicitly, because setCurrentIndex() above only emits when the index
	// actually changes - and the stored codec is often the one already showing.
	callAudioCodecChanged();

#ifdef HAVE_VAAPI
	cbVaapiEncoding->setChecked(st.loadValue("video.vaapi_encoding", 0).toBool());
	cbVaapiDecoding->setChecked(st.loadValue("video.vaapi_decoding", 0).toBool());
	cbVaapiRendering->setChecked(st.loadValue("video.vaapi_rendering", 0).toBool());
#endif

	QString audioCapDevVal = st.loadValue("audio.capture_device", "default").toString();
	int audioCapDevItem = audioCaptureDev->findData(audioCapDevVal);
	if (audioCapDevItem >= 0)
		audioCaptureDev->setCurrentIndex(audioCapDevItem);

	QString audioPlaybackDevVal = st.loadValue("audio.playback_device", "default").toString();
	int audioPlaybackDevItem = audioPlaybackDev->findData(audioPlaybackDevVal);
	if (audioPlaybackDevItem >= 0)
		audioPlaybackDev->setCurrentIndex(audioPlaybackDevItem);

	// Not read from st directly: the setting has a default that depends on the
	// platform, and an older on/off setting to fall back on.
	switch (loadEchoCancellationSetting())
	{
	case EchoCancellationPlatform:
		rbEchoCancelPlatform->setChecked(true);
		break;

	case EchoCancellationNone:
		rbEchoCancelNone->setChecked(true);
		break;

	case EchoCancellationOwn:
	default:
		rbEchoCancelOwn->setChecked(true);
		break;
	}

	// Covers a saved "platform" on a machine where it is no longer on offer -
	// a Linux install whose audio system has since been moved to ALSA.
	updateEchoCancellationChoices();

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
	st.storeValue("video.capture_width", QString::number(selectedVideoWidth()));
	st.storeValue("video.capture_height", QString::number(selectedVideoHeight()));
	st.storeValue("video.capture_framerate", QString::number(selectedVideoFrameRate()));
	st.storeValue("video.prefer_compressed", cbPreferCompressed->isChecked() ? "1" : "0");

	st.storeValue("call.video_codec", callVideoCodec->itemData(callVideoCodec->currentIndex()).toString());
	st.storeValue("call.audio_codec", callAudioCodec->itemData(callAudioCodec->currentIndex()).toString());
	st.storeValue("call.video_bitrate_kbps", QString::number(callVideoBitrate->value()));

	// The box shows a fixed rate codec's own bitrate while such a codec is
	// selected; what gets stored is the user's choice either way, so that
	// switching back to Opus finds it again on the next visit.
	if (callAudioBitrate->isEnabled())
		audioBitrateKbps = callAudioBitrate->value();

	st.storeValue("call.audio_bitrate_kbps", QString::number(audioBitrateKbps));
#ifdef HAVE_VAAPI
	st.storeValue("video.vaapi_encoding", cbVaapiEncoding->isChecked() ? "1" : "0");
	st.storeValue("video.vaapi_decoding", cbVaapiDecoding->isChecked() ? "1" : "0");
	st.storeValue("video.vaapi_rendering", cbVaapiRendering->isChecked() ? "1" : "0");
#endif
	st.storeValue("audio.capture_device", audioCaptureDev->itemData(audioCaptureDev->currentIndex()).toString());
	st.storeValue("audio.playback_device", audioPlaybackDev->itemData(audioPlaybackDev->currentIndex()).toString());
	st.storeValue("audio.ring_device", audioRingDev->itemData(audioRingDev->currentIndex()).toString());

	if (audioDriver)
	{
		QString drv = audioDriver->itemData(audioDriver->currentIndex()).toString();
		st.storeValue("audio.driver", drv);
		AudioDevice::SetLinuxDriverName(drv);
	}

	// After the driver, since on Linux that is what decides whether the
	// platform option means anything - and updateEchoCancellationChoices()
	// has already moved the selection off it if it does not.
	EchoCancellationMode echoMode = EchoCancellationOwn;

	if (rbEchoCancelPlatform->isChecked())
		echoMode = EchoCancellationPlatform;
	else if (rbEchoCancelNone->isChecked())
		echoMode = EchoCancellationNone;

	storeEchoCancellationSetting(echoMode);

	// Publish it to the audio threads, the same way the driver above is
	// published - see AudioDevice::SetEchoCancellation().
	AudioDevice::SetEchoCancellation(echoMode);
}
