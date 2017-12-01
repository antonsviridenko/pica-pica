#include "filetransferdialog.h"
#include "../contacts.h"
#include "../accounts.h"
#include "../globals.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <stdlib.h>

FileTransferDialog::FileTransferDialog(QByteArray peer_id, QString filename, quint64 size,
                                       TransferDirection drct, QWidget *parent) :
	peer_id_(peer_id),
	QDialog(parent),
	dir_(drct),
	filename_(filename),
	filesize_(size)
{
	Contacts ct(config_dbname, Accounts::GetCurrentAccount().id);
	peer_name_ = ct.GetContactName(peer_id_);

	if (peer_name_.isEmpty())
		peer_name_ = peer_id_.toBase64();
	else
		peer_name_ = QString("%1 (%2...)").arg(peer_name_).arg(peer_id_.toBase64().left(8).constData());

	QVBoxLayout *layout = new QVBoxLayout(this);
	QHBoxLayout *btlayout = new QHBoxLayout();

	lbFilename = new QLabel(this);
	lbPeer = new QLabel(this);
	lbProgressStatus = new QLabel(this);
	lbTransferSpeed = new QLabel(this);
	lbRemainingTime = new QLabel(this);
	lbTransferStatus = new QLabel(this);

	pgbar = new QProgressBar(this);
	leftbutton = new QPushButton(this);
	rightbutton = new QPushButton(this);

	layout->addWidget(lbFilename);
	layout->addWidget(lbPeer);
	layout->addWidget(lbProgressStatus);
	layout->addWidget(lbTransferSpeed);
	layout->addWidget(lbRemainingTime);
	layout->addWidget(pgbar);
	layout->addWidget(lbTransferStatus);
	layout->addLayout(btlayout);

	btlayout->addWidget(leftbutton);
	btlayout->addWidget(rightbutton);

	if (drct == RECEIVING)
	{
		leftbutton->setText(tr("Accept"));
		rightbutton->setText(tr("Deny"));

		lbPeer->setText("Sender: " + peer_name_);

		setWindowTitle(tr("Receiving file %1 from %2").arg(filename_).arg(peer_name_));
	}
	else if (drct == SENDING)
	{
		leftbutton->setText(tr("Pause"));
		rightbutton->setText(tr("Cancel"));
		leftbutton->setEnabled(false);

		lbPeer->setText("Receiver: " + peer_name_);

		setWindowTitle(tr("Sending file %1 to %2").arg(filename_).arg(peer_name_));
	}
	setTransferStatus(WAITINGFORACCEPT);

	lbFilename->setText(filename_);

	pgbar->setMinimum(0);
	pgbar->setMaximum(100);

	update(0);

	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(timeout()));
	timer->start(1000);
	progress_ = 0;
	prevprogress_ = 0;

	connect(leftbutton, SIGNAL(clicked()), this, SLOT(leftbuttonclick()));
	connect(rightbutton, SIGNAL(clicked()), this, SLOT(rightbuttonclick()));
}

void FileTransferDialog::update(quint64 progress)
{
	progress_ = progress;
	percents = (double)progress / filesize_ * 100.0;

	lbProgressStatus->setText(bytestoHumanBase2(progress) + " / "
	                          + bytestoHumanBase2(filesize_) + QString(" %1 %").arg((int)percents));

	pgbar->setValue(percents);

	if (progress == filesize_)
	{
		setTransferStatus(FINISHED);
	}
}

void FileTransferDialog::setTransferStatus(TransferStatus st)
{
	bool isTerminalState = false;

	switch(st)
	{
	case WAITINGFORACCEPT:
		if (dir_ == SENDING)
			lbTransferStatus->setText(tr("Waiting for peer to accept the file"));
		else
			lbTransferStatus->setText(tr("Waiting for acceptance"));
		break;

	case DENIED:
		lbTransferStatus->setText(tr("Denied by peer"));
		isTerminalState = true;
		break;

	case SENDINGFILE:
		leftbutton->setEnabled(true);
		lbTransferStatus->setText(tr("Sending file"));
		break;

	case RECEIVINGFILE:
		lbTransferStatus->setText(tr("Receiving file"));
		break;

	case PAUSED:
		lbTransferStatus->setText(tr("Paused"));
		break;

	case CANCELLED:
		lbTransferStatus->setText(tr("Cancelled"));
		isTerminalState = true;
		break;

	case FINISHED:
		lbTransferStatus->setText(tr("Finished"));
		isTerminalState = true;
		break;

	case PEERDISCONNECTED:
		lbTransferStatus->setText(tr("Peer disconnected"));
		isTerminalState = true;
		break;

	case IOERROR:
		lbTransferStatus->setText(tr("I/O error"));
		isTerminalState = true;
		break;
	}

	if (isTerminalState)
	{
		leftbutton->setEnabled(false);
		rightbutton->setEnabled(false);

		QWidget::setAttribute(Qt::WA_DeleteOnClose);
		QWidget::setAttribute(Qt::WA_QuitOnClose, false);
	}
}

void FileTransferDialog::pausedByPeer()
{
	leftbutton->setEnabled(false);
	setTransferStatus(PAUSED);
}

void FileTransferDialog::resumedByPeer()
{
	leftbutton->setEnabled(true);
	setTransferStatus( dir_ == SENDING ? SENDINGFILE : RECEIVINGFILE);
}

void FileTransferDialog::cancelledByPeer()
{
	setTransferStatus(CANCELLED);
}

void FileTransferDialog::ioError()
{
	setTransferStatus(IOERROR);
}

void FileTransferDialog::peerDisconnected()
{
	setTransferStatus(PEERDISCONNECTED);
}

void FileTransferDialog::leftbuttonclick()
{
	if (dir_ == RECEIVING)
	{
		if (leftbutton->text() == tr("Accept"))
		{
			leftbutton->setText("Pause");
			rightbutton->setText("Cancel");
			emit acceptedFile(peer_id_);
			return;
		}
	}

	if (leftbutton->text() == tr("Pause"))
	{
		leftbutton->setText(tr("Resume"));
		emit pausedFile(peer_id_, this);
		setTransferStatus(PAUSED);
	}
	else if (leftbutton->text() == tr("Resume"))
	{
		leftbutton->setText(tr("Pause"));
		emit resumedFile(peer_id_, this);
		setTransferStatus(dir_ == SENDING ? SENDINGFILE : RECEIVINGFILE);
	}
}

void FileTransferDialog::rightbuttonclick()
{
	if (dir_ == RECEIVING)
	{
		if (rightbutton->text() == tr("Deny"))
		{
			emit deniedFile(peer_id_);
			setTransferStatus(DENIED);
			return;
		}
	}

	if (rightbutton->text() == tr("Cancel"))
	{

		if (QMessageBox::No == QMessageBox::question(this, tr("Cancel File Transfer"),
		        tr("Are you sure you want to cancel file transfer?"),
		        QMessageBox::Yes | QMessageBox::No, QMessageBox::No))
			return;

		emit cancelledFile(peer_id_, this);
		setTransferStatus(CANCELLED);
	}
}

QString FileTransferDialog::bytestoHumanBase2(quint64 bytes)
{
	const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
	int i = 0;
	double size = bytes;

	while (size > 1024)
	{
		size /= 1024;
		i++;
	}

	return QString("%1 %2").arg(size, 6, 'f', i ? 2 : 0).arg(units[i]);
}

QString FileTransferDialog::bytestoHumanBase10(quint64 bytes)
{
	const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
	int i = 0;
	double size = bytes;

	while (size > 1000)
	{
		size /= 1000;
		i++;
	}

	return QString("%1 %2").arg(size, 6, 'f', i ? 2 : 0).arg(units[i]);
}

QString FileTransferDialog::timeLeft(quint64 seconds)
{
	if (seconds < 60)
	{
		return QString(tr("%1 seconds")).arg(seconds);
	}

	int minutes = seconds / 60;
	int hours = minutes / 60;
	double days = (double)hours / 24.0;

	if (days > 1.0 )
		return QString(tr("%1 days")).arg(days, 6, 'f', 1);

	if (hours > 0)
		return QString("%1h:%2m").arg(hours).arg(minutes % 60);

	return QString("%1:%2").arg(minutes).arg((double)(seconds % 60), 2, 'f', 0, '0');
}

void FileTransferDialog::timeout()
{
	if (progress_ >= filesize_)
		return;

	quint64 bps = progress_ - prevprogress_;
	prevprogress_ = progress_;

	if (bps != 0)
	{
		quint64 secondsleft = (filesize_ - progress_) / bps;

		lbRemainingTime->setText(timeLeft(secondsleft));
	}
	else
		lbRemainingTime->setText(QString());

	lbTransferSpeed->setText(bytestoHumanBase10(bps) + tr("/s"));
}
