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

class SettingsDialog : public QDialog
{
	Q_OBJECT
public:
	explicit SettingsDialog(QWidget *parent = 0);

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
	QComboBox *videoDev;
	QPushButton *videoDevRefresh;

	QPushButton *btAudioTest;

	QPushButton *btOk;
	QPushButton *btCancel;

	void loadSettings();
	void storeSettings();

private slots:
	void OK();
	void Cancel();
	void toggleIncomingConnections(bool checked);
	void toggleMultipleLogins(bool checked);
	void fillVideoDevices();

};

#endif // SETTINGSDIALOG_H
