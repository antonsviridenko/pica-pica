#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QRadioButton>

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

	QLineEdit *addr;
	QSpinBox *publicPort;
	QSpinBox *localPort;

	QPushButton *btOk;
	QPushButton *btCancel;


};

#endif // SETTINGSDIALOG_H
