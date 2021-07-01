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

	audioCaptureDev = new QComboBox(this);
	audioCaptureDev->addItem(QString("default"));

	audioPlaybackDev = new QComboBox(this);
	audioPlaybackDev->addItem(QString("default"));

	btAudioTest = new QPushButton(tr("Test"), this);

	audiodevlayout->addWidget(lbAudioCaptureDev);
	audiodevlayout->addWidget(audioCaptureDev);
	audiodevlayout->addWidget(lbAudioPlaybackDev);
	audiodevlayout->addWidget(audioPlaybackDev);
	audiodevlayout->addWidget(btAudioTest);
	audiodevlayout->addStretch(1);

	audiodevtab->setLayout(audiodevlayout);

//Video Devices
	videoDev = new QComboBox(this);
	//videoDev->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	//videoDev->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	videoDev->setMinimumHeight(videoDev->height() * 2);
	fillVideoDevices();
	videoDevRefresh = new QPushButton(tr("Refresh 🔄"), this);
	connect(videoDevRefresh, SIGNAL(clicked()), this, SLOT(fillVideoDevices()));
	videodevLayout->addWidget(videoDev);
	videodevLayout->addWidget(videoDevRefresh);
	videodevLayout->addStretch(1);

	videodevtab->setLayout(videodevLayout);

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

void SettingsDialog::fillVideoDevices()
{
		videoDev->clear();
		QList<MediaDeviceInfo> vd = VideoDevice().Enumerate();
		for (int i = 0; i < vd.size(); i++)
		{
			QString item = QString(QLatin1String("%2\n(%1)"))
									.arg(vd.at(i).device)
									.arg(vd.at(i).humanReadable);
			videoDev->addItem(item, vd.at(i).device);
		}
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

	QString videoCapDev = st.loadValue("video.capture_device", QString()).toString();
	int videoDevItem = videoDev->findData(videoCapDev);
	if (videoDevItem >= 0)
		videoDev->setCurrentIndex(videoDevItem);
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

	QString videoCapDev;
	st.storeValue("video.capture_device", videoDev->itemData(videoDev->currentIndex()).toString());
}
