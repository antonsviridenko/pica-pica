#include "settingsdialog.h"
#include "../settings.h"
#include "../globals.h"
#include "../../PICA_netconf.h"

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

	//QGroupBox *groupBox = new QGroupBox(tr("Direct Connections"), directc2ctab);
	QVBoxLayout *directc2cLayout = new QVBoxLayout();

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

	//groupBox->setLayout(directc2cLayout);
	directc2ctab->setLayout(directc2cLayout);

	//settingsLayout->addWidget(groupBox);
	tabW->addTab(directc2ctab, tr("Direct Connections"));
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

void SettingsDialog::toggleIncomingConnections(bool checked)
{
	addr->setEnabled(checked);
	publicPort->setEnabled(checked);
	localPort->setEnabled(checked);
#ifdef HAVE_LIBMINIUPNPC
	cbEnableUPnP->setEnabled(checked);
#endif
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
}
