#ifndef FORGEDCERTDIALOG_H
#define FORGEDCERTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include "viewcertdialog.h"

class ForgedCertDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ForgedCertDialog(QByteArray peer_id, QString received_cert, QString stored_cert, QWidget *parent = 0);

private:
	QString received_cert_;
	QString stored_cert_;

	QPushButton *btOK;
	QPushButton *btShow_stored_cert;
	QPushButton *btShow_received_cert;


signals:

public slots:

private slots:
	void OK();
	void StoredCert();
	void ReceivedCert();

};

#endif // FORGEDCERTDIALOG_H
