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
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QRadioButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QThread>
#include <QImage>
#include <QList>
#include <QTimer>
#include "../mediadevice.h"
#include "../audiodevice.h"
#include "../videodevice.h"
#include "../toneplayer/toneplayer.h"
#ifdef HAVE_VAAPI
#include "../vaapivideowidget.h"
#endif

class SettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SettingsDialog(QWidget *parent = 0);
	~SettingsDialog();

signals:

public slots:
private slots:

private:
	QRadioButton *rbDisableDirectConns;
	QRadioButton *rbEnableOutgoingConns;
	QRadioButton *rbEnableIncomingConns;
#ifdef HAVE_LIBMINIUPNPC
	QCheckBox *cbEnableUPnP;
#endif

	QComboBox *addr;
	QSpinBox *publicPort;
	QSpinBox *localPort;

	QRadioButton *rbMLPProhibit;
	QRadioButton *rbMLPReplace;
	QRadioButton *rbMLPAllowMultiple;

	QComboBox *audioCaptureDev;
	QComboBox *audioPlaybackDev;
	QComboBox *audioRingDev;
	QComboBox *videoDev;
	QPushButton *videoDevRefresh;

	// Local audio pipeline test: a few seconds of the selected microphone are
	// captured and encoded, then decoded again and played back through the
	// selected playback device - the same path a call takes, minus the
	// network. Recording and playback are deliberately kept apart in time
	// rather than looped back live, which would howl with acoustic feedback
	// when the test runs on speakers.
	QPushButton *btAudioTest;
	QLabel *audioTestStatus;
	AudioDevice *testMic;
	AudioDevice *testSpeaker;
	QThread testMicThread;
	QThread testSpeakerThread;

	enum AudioTestState
	{
		AudioTestIdle,
		AudioTestRecording,
		AudioTestPlaying
	};

	AudioTestState audioTestState;
	// The recording, as encoded packets, and how far playback has fed it.
	QList<QByteArray> audioTestPackets;
	int audioTestPlaybackPos;
	// Members rather than QTimer::singleShot() calls so that stopping a test
	// cancels them - otherwise a timer left over from an aborted run would
	// fire into the next one.
	QTimer *audioTestRecordTimer;
	QTimer *audioTestPlaybackTimer;
	QTimer *audioTestDrainTimer;

	void stopAudioTest();
	void startAudioTestPlayback();

	// Ring device test: plays the incoming call bell through the selected
	// ring device, using the same TonePlayer an actual incoming call uses.
	// TonePlayer::play() blocks its thread for the length of the tone
	// sequence, hence the dedicated thread.
	QPushButton *btRingTest;
	QLabel *ringTestStatus;
	TonePlayer *ringTestPlayer;
	QThread ringTestPlayerThread;
	bool ringTestRunning;

	// Local video pipeline test: frames from the selected camera are
	// encoded, split into protocol sized fragments, reassembled and decoded
	// again, then shown in videoPreview - the same path a call takes, minus
	// the network, so the whole chain can be checked from this dialog.
	QPushButton *btVideoTest;
	QLabel *videoPreview;
	// Shown under the preview: the format the camera settled on, and whether
	// the GPU or the CPU ended up doing the encoding and decoding.
	QLabel *videoTestStatus;
	QString videoTestFormat;
	QString videoTestPathReport;
	QCheckBox *cbPreferCompressed;
#ifdef HAVE_VAAPI
	QCheckBox *cbVaapiEncoding;
	QCheckBox *cbVaapiDecoding;
	QCheckBox *cbVaapiRendering;
	// Shown in place of videoPreview while frames are being drawn straight
	// from GPU memory; only one of the two is ever visible.
	VaapiVideoWidget *videoPreviewGpu;
#endif
	VideoDevice *testCam;
	VideoDevice *testDecoder;
	QThread testCamThread;
	QThread testDecoderThread;
	VideoFrameAssembler testAssembler;
	bool videoTestRunning;

	void stopVideoTest();

	QPushButton *btOk;
	QPushButton *btCancel;

	void loadSettings();
	void storeSettings();

	void fillDevicesComboBox(QComboBox *cb, MediaDevice *dev, enum MediaDeviceStreamDirection dir);

private slots:
	void OK();
	void Cancel();
	void toggleIncomingConnections(bool checked);
	void toggleMultipleLogins(bool checked);
	void fillVideoDevices();
	void fillAudioCaptureDevices();
	void fillAudioPlaybackDevices();
	void fillAudioRingDevices();

	void toggleRingTest();
	void ringTestFinished();
	void ringTestError(QString message);

	void toggleAudioTest();
	void audioTestPacket(QByteArray data);
	void audioTestRecordingFinished();
	void audioTestFeedPacket();
	void audioTestPlaybackFinished();
	void audioTestError(QString message);

	void toggleVideoTest();
	void videoTestCaptureStarted(QString codec, int width, int height);
	void videoTestPath(QString description);
	void videoTestFragment(QByteArray data, bool is_last_fragment);
	void videoTestFrame(QImage frame);
#ifdef HAVE_VAAPI
	void videoTestHwFrame(AVFramePtr frame);
	void videoTestRenderFailed(QString message);
#endif
	void videoTestError(QString message);

};

#endif // SETTINGSDIALOG_H
