#include "settingsdialog.h"
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>

SettingsDialog::SettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	QVBoxLayout *settingsLayout = new QVBoxLayout();

	QGroupBox *groupBox = new QGroupBox(tr("Direct Connections"), this);
	QVBoxLayout *directc2cLayout = new QVBoxLayout();

	rbDisableDirectConns = new QRadioButton(tr("Disable direct connections"), this);
	rbEnableOutgoingConns = new QRadioButton(tr("Enable only outgoing direct connections"), this);
	rbEnableIncomingConns = new QRadioButton(tr("Enable incoming direct connections"), this);

	rbEnableIncomingConns->setChecked(true);

	QLabel *lbAddr = new QLabel(tr("Public address for incoming connections"), this);
	addr = new QLineEdit(tr("0.0.0.0"), this);
	publicPort = new QSpinBox(this);
	localPort = new QSpinBox(this);

	publicPort->setRange(1, 65535);
	localPort->setRange(1, 65535);

	publicPort->setValue(2298);
	localPort->setValue(2298);



	directc2cLayout->addWidget(rbDisableDirectConns);
	directc2cLayout->addWidget(rbEnableOutgoingConns);
	directc2cLayout->addWidget(rbEnableIncomingConns);
	directc2cLayout->addWidget(lbAddr);
	directc2cLayout->addWidget(addr);
	directc2cLayout->addWidget(publicPort);
	directc2cLayout->addWidget(localPort);
	directc2cLayout->addStretch(1);

	groupBox->setLayout(directc2cLayout);

	settingsLayout->addWidget(groupBox);

	setLayout(settingsLayout);
}
