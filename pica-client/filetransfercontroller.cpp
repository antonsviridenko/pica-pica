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
#include "filetransfercontroller.h"
#include "globals.h"
#include "skynet.h"
#include "mainwindow.h"
#include "msguirouter.h"
#include <QFileDialog>
#include <QLocale>
#include <QFileInfo>

FileTransferController::FileTransferController(QObject *parent) :
	QObject(parent)
{
	connect(skynet, SIGNAL(IncomingFileRequestReceived(QByteArray, quint64, QString)),
	        this, SLOT(file_request(QByteArray, quint64, QString)));

	connect(skynet, SIGNAL(FileProgress(QByteArray, quint64, quint64)),
	        this, SLOT(file_progress(QByteArray, quint64, quint64)));

	connect(skynet, SIGNAL(OutgoingFileRequestAccepted(QByteArray)), this, SLOT(file_accepted_by_peer(QByteArray)));

	connect(skynet, SIGNAL(OutgoingFileRequestDenied(QByteArray)), this, SLOT(file_denied_by_peer(QByteArray)));

	connect(skynet, SIGNAL(c2cClosed(QByteArray)), this, SLOT(file_peer_disconnected(QByteArray)));
	connect(this, SIGNAL(transfer_note(QByteArray, QString)), msguirouter, SLOT(transfer_note(QByteArray, QString)));
}

void FileTransferController::file_peer_disconnected(QByteArray peer_id)
{
	if (ftdialogs_in.contains(peer_id))
	{
		ftdialogs_in[peer_id]->peerDisconnected();
		ftdialogs_in.remove(peer_id);
		emit transfer_note(peer_id, tr("Receiving \"%1\" was interrupted: disconnected").arg(fnames[peer_id]));
	}

	if (ftdialogs_out.contains(peer_id))
	{
		ftdialogs_out[peer_id]->peerDisconnected();
		ftdialogs_out.remove(peer_id);
		emit transfer_note(peer_id, tr("Sending \"%1\" was interrupted: disconnected").arg(fnames_out[peer_id]));
	}
}

void FileTransferController::file_finished_incoming(QByteArray peer_id)
{
	if (ftdialogs_in.contains(peer_id))
	{
		ftdialogs_in.remove(peer_id);
		emit transfer_note(peer_id, tr("Received \"%1\"").arg(fnames[peer_id]));
	}
}

void FileTransferController::file_finished_outgoing(QByteArray peer_id)
{
	if (ftdialogs_out.contains(peer_id))
	{
		ftdialogs_out.remove(peer_id);
		qDebug() << "outgoing file transfer finished, removing dialog pointer\n";
		emit transfer_note(peer_id, tr("Sent \"%1\"").arg(fnames_out[peer_id]));
	}
}

void FileTransferController::file_progress(QByteArray peer_id, quint64 bytes_sent, quint64 bytes_received)
{
	if (bytes_received > 0)
	{
		if (ftdialogs_in.contains(peer_id))
		{
			ftdialogs_in[peer_id]->update(bytes_received);
		}
	}
	else if (bytes_sent > 0)
	{
		if (ftdialogs_out.contains(peer_id))
		{
			ftdialogs_out[peer_id]->update(bytes_sent);
		}
	}
}

void FileTransferController::file_request(QByteArray peer_id, quint64 file_size, QString filename)
{
	FileTransferDialog *ftd = new FileTransferDialog(peer_id, filename, file_size, FileTransferDialog::RECEIVING);

	ftdialogs_in[peer_id] = ftd;
	fnames[peer_id] = filename;
	emit transfer_note(peer_id, tr("Incoming file \"%1\" (%2)").arg(filename, QLocale().formattedDataSize(file_size)));

	connect(ftd, SIGNAL(acceptedFile(QByteArray)), this, SLOT(file_accepted_by_me(QByteArray)));
	connect(ftd, SIGNAL(deniedFile(QByteArray)), this, SLOT(file_denied_by_me(QByteArray)));

	ftd->show();
}

void FileTransferController::file_accepted_by_peer(QByteArray peer_id)
{
	connect(skynet, SIGNAL(OutgoingFilePaused(QByteArray)), this, SLOT(file_paused_outgoing(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(OutgoingFileResumed(QByteArray)), this, SLOT(file_resumed_outgoing(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(OutgoingFileCancelled(QByteArray)), this, SLOT(file_cancelled_outgoing(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(OutgoingFileIoError(QByteArray)), this, SLOT(file_ioerror_outgoing(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(OutgoingFileFinished(QByteArray)), this, SLOT(file_finished_outgoing(QByteArray)), Qt::UniqueConnection);

	ftdialogs_out[peer_id]->setTransferStatus(FileTransferDialog::SENDINGFILE);
}

void FileTransferController::file_denied_by_peer(QByteArray peer_id)
{
	ftdialogs_out[peer_id]->setTransferStatus(FileTransferDialog::DENIED);
	emit transfer_note(peer_id, tr("\"%1\" was declined").arg(fnames_out[peer_id]));
}

void FileTransferController::file_paused_incoming(QByteArray peer_id)
{
	ftdialogs_in[peer_id]->pausedByPeer();
}

void FileTransferController::file_resumed_incoming(QByteArray peer_id)
{
	ftdialogs_in[peer_id]->resumedByPeer();
}

void FileTransferController::file_cancelled_incoming(QByteArray peer_id)
{
	ftdialogs_in[peer_id]->cancelledByPeer();
	ftdialogs_in.remove(peer_id);
	emit transfer_note(peer_id, tr("Receiving \"%1\" was cancelled by the sender").arg(fnames[peer_id]));
}

void FileTransferController::file_ioerror_incoming(QByteArray peer_id)
{
	ftdialogs_in[peer_id]->ioError();
	ftdialogs_in.remove(peer_id);
	emit transfer_note(peer_id, tr("Receiving \"%1\" failed").arg(fnames[peer_id]));
}

void FileTransferController::file_paused_outgoing(QByteArray peer_id)
{
	ftdialogs_out[peer_id]->pausedByPeer();
}

void FileTransferController::file_resumed_outgoing(QByteArray peer_id)
{
	ftdialogs_out[peer_id]->resumedByPeer();

}

void FileTransferController::file_cancelled_outgoing(QByteArray peer_id)
{
	ftdialogs_out[peer_id]->cancelledByPeer();
	ftdialogs_out.remove(peer_id);
	emit transfer_note(peer_id, tr("Sending \"%1\" was cancelled by the receiver").arg(fnames_out[peer_id]));
}

void FileTransferController::file_ioerror_outgoing(QByteArray peer_id)
{
	ftdialogs_out[peer_id]->ioError();
	ftdialogs_out.remove(peer_id);
	emit transfer_note(peer_id, tr("Sending \"%1\" failed").arg(fnames_out[peer_id]));
}

void FileTransferController::file_accepted_by_me(QByteArray peer_id)
{
	QString filePathToSave = QFileDialog::getSaveFileName(ftdialogs_in[peer_id], tr("Save received file to"),
	                         fnames[peer_id], tr("Any file (* *.*)"));

	skynet->AcceptFile(peer_id, filePathToSave);

	connect(ftdialogs_in[peer_id], SIGNAL(cancelledFile(QByteArray, FileTransferDialog*)), this, SLOT(file_cancelled_by_me(QByteArray, FileTransferDialog*)));
	connect(ftdialogs_in[peer_id], SIGNAL(pausedFile(QByteArray, FileTransferDialog*)), this, SLOT(file_paused_by_me(QByteArray, FileTransferDialog*)));
	connect(ftdialogs_in[peer_id], SIGNAL(resumedFile(QByteArray, FileTransferDialog*)), this, SLOT(file_resumed_by_me(QByteArray, FileTransferDialog*)));

	connect(skynet, SIGNAL(IncomingFilePaused(QByteArray)), this, SLOT(file_paused_incoming(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(IncomingFileResumed(QByteArray)), this, SLOT(file_resumed_incoming(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(IncomingFileCancelled(QByteArray)), this, SLOT(file_cancelled_incoming(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(IncomingFileIoError(QByteArray)), this, SLOT(file_ioerror_incoming(QByteArray)), Qt::UniqueConnection);
	connect(skynet, SIGNAL(IncomingFileFinished(QByteArray)), this, SLOT(file_finished_incoming(QByteArray)), Qt::UniqueConnection);

	ftdialogs_in[peer_id]->setTransferStatus(FileTransferDialog::RECEIVINGFILE);
}

void FileTransferController::file_denied_by_me(QByteArray peer_id)
{
	skynet->DenyFile(peer_id);

	//delete ftdialogs_in[peer_id];

	ftdialogs_in.remove(peer_id);
}

void FileTransferController::file_cancelled_by_me(QByteArray peer_id, FileTransferDialog *from)
{
	bool sending = true;

	if (from->getTransferDirection() == FileTransferDialog::RECEIVING)
	{
		ftdialogs_in.remove(peer_id);

		sending = false;
	}
	else
	{
		ftdialogs_out.remove(peer_id);
	}

	emit transfer_note(peer_id, tr("\"%1\" was cancelled").arg(sending ? fnames_out[peer_id] : fnames[peer_id]));

	skynet->CancelFile(peer_id, sending);
}

void FileTransferController::file_paused_by_me(QByteArray peer_id, FileTransferDialog *from)
{
	bool sending = true;

	if (from->getTransferDirection() == FileTransferDialog::RECEIVING)
		sending = false;

	skynet->PauseFile(peer_id, sending);
}

void FileTransferController::file_resumed_by_me(QByteArray peer_id, FileTransferDialog *from)
{
	bool sending = true;

	if (from->getTransferDirection() == FileTransferDialog::RECEIVING)
		sending = false;

	skynet->ResumeFile(peer_id, sending);
}

void FileTransferController::send_file(QByteArray peer_id)
{

	if (ftdialogs_out.contains(peer_id))
	{
		msguirouter->notification(tr("There is already file transfer in progress. Wait until it is finished."), true);
		return;
	}

	QString filepath = QFileDialog::getOpenFileName(0, tr("Select file to send"), QString(), tr("Any file (* *.*)"));

	if (filepath.isNull())
		return;

	QFileInfo fi(filepath);

	if (!fi.exists())
	{
		msguirouter->notification(tr("File \"%1\" does not exist.").arg(filepath), true);
		return;
	}

	if (fi.size() == 0)
	{
		msguirouter->notification(tr("Cannot send empty file."), true);
		return;
	}

	skynet->SendFile(peer_id, filepath);

	FileTransferDialog *ftd = new FileTransferDialog(peer_id, fi.fileName(), fi.size(), FileTransferDialog::SENDING);

	ftdialogs_out[peer_id] = ftd;
	fnames_out[peer_id] = fi.fileName();
	emit transfer_note(peer_id, tr("Sending \"%1\" (%2)").arg(fi.fileName(), QLocale().formattedDataSize(fi.size())));

	connect(ftdialogs_out[peer_id], SIGNAL(cancelledFile(QByteArray, FileTransferDialog*)), this, SLOT(file_cancelled_by_me(QByteArray, FileTransferDialog*)));
	connect(ftdialogs_out[peer_id], SIGNAL(pausedFile(QByteArray, FileTransferDialog*)), this, SLOT(file_paused_by_me(QByteArray, FileTransferDialog*)));
	connect(ftdialogs_out[peer_id], SIGNAL(resumedFile(QByteArray, FileTransferDialog*)), this, SLOT(file_resumed_by_me(QByteArray, FileTransferDialog*)));

	ftd->show();
}
