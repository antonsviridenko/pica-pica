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

	QPushButton *btOk;
	QPushButton *btCancel;

	void loadSettings();
	void storeSettings();

private slots:
	void OK();
	void Cancel();
	void toggleIncomingConnections(bool checked);
	void toggleMultipleLogins(bool checked);

};

#endif // SETTINGSDIALOG_H
