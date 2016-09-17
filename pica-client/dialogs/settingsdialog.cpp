#include "settingsdialog.h"
#include "../settings.h"
#include "../globals.h"

#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVariant>

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

    addr->setText(st.loadValue("direct_c2c.public_addr", "0.0.0.0").toString());

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

    st.storeValue("direct_c2c.public_addr", addr->text());
    st.storeValue("direct_c2c.public_port", QString::number(publicPort->value()));
    st.storeValue("direct_c2c.local_port", QString::number(localPort->value()));
}
