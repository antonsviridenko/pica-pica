/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef FILETRANSFERDIALOG_H
#define FILETRANSFERDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QByteArray>

class FileTransferDialog : public QDialog
{
	Q_OBJECT
public:
	enum TransferDirection
	{
		SENDING,
		RECEIVING
	};

	enum TransferStatus
	{
		WAITINGFORACCEPT,
		DENIED,
		SENDINGFILE,
		RECEIVINGFILE,
		PAUSED,
		CANCELLED,
		PEERDISCONNECTED,
		IOERROR,
		FINISHED
	};

	explicit FileTransferDialog(QByteArray peer_id, QString filename, quint64 size,
	                            TransferDirection drct, QWidget *parent = 0);

	TransferDirection getTransferDirection()
	{
		return dir_;
	};


private:
	QLabel *lbFilename;
	QLabel *lbPeer;
	QLabel *lbProgressStatus;
	QLabel *lbTransferSpeed;
	QLabel *lbRemainingTime;
	QLabel *lbTransferStatus;
	QProgressBar *pgbar;
	QPushButton *leftbutton;
	QPushButton *rightbutton;

	QByteArray peer_id_;
	QString peer_name_;
	TransferDirection dir_;
	QString filename_;
	quint64 filesize_;

	double percents;
	quint64 progress_;
	quint64 prevprogress_;

	static QString bytestoHumanBase2(quint64 bytes);
	static QString bytestoHumanBase10(quint64 bytes);
	static QString timeLeft(quint64 seconds);

	QTimer *timer;
signals:
	void pausedFile(QByteArray peer_id, FileTransferDialog *sender);
	void resumedFile(QByteArray peer_id, FileTransferDialog *sender);
	void cancelledFile(QByteArray peer_id, FileTransferDialog *sender);
	void acceptedFile(QByteArray peer_id);
	void deniedFile(QByteArray peer_id);

public slots:
	void update(quint64 progress);
	void setTransferStatus(enum TransferStatus st);
	void pausedByPeer();
	void resumedByPeer();
	void cancelledByPeer();
	void ioError();
	void peerDisconnected();

private slots:
	void timeout();
	void leftbuttonclick();
	void rightbuttonclick();

};

#endif // FILETRANSFERDIALOG_H
