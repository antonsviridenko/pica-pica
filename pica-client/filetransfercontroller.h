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
#ifndef FILETRANSFERCONTROLLER_H
#define FILETRANSFERCONTROLLER_H

#include <QObject>
#include <QMap>
#include "dialogs/filetransferdialog.h"

class FileTransferController : public QObject
{
	Q_OBJECT
public:
	explicit FileTransferController(QObject *parent = 0);

signals:

private:

	QMap<QByteArray, FileTransferDialog*> ftdialogs_in;
	QMap<QByteArray, FileTransferDialog*> ftdialogs_out;

	QMap<QByteArray, QString> fnames;


public slots:

	// from remote peer to me
	void file_progress(QByteArray peer_id, quint64 bytes_sent, quint64 bytes_received);
	void file_request(QByteArray peer_id, quint64 file_size, QString filename);
	void file_accepted_by_peer(QByteArray peer_id);
	void file_denied_by_peer(QByteArray peer_id);

	void file_paused_incoming(QByteArray peer_id);
	void file_resumed_incoming(QByteArray peer_id);
	void file_cancelled_incoming(QByteArray peer_id);
	void file_ioerror_incoming(QByteArray peer_id);

	void file_paused_outgoing(QByteArray peer_id);
	void file_resumed_outgoing(QByteArray peer_id);
	void file_cancelled_outgoing(QByteArray peer_id);
	void file_ioerror_outgoing(QByteArray peer_id);

	void file_finished_incoming(QByteArray peer_id);
	void file_peer_disconnected(QByteArray peer_id);

	//from me (file transfer dialog shown to user) to remote peer
	void file_accepted_by_me(QByteArray peer_id);
	void file_denied_by_me(QByteArray peer_id);
	void file_cancelled_by_me(QByteArray peer_id, FileTransferDialog *from);
	void file_paused_by_me(QByteArray peer_id, FileTransferDialog *from);
	void file_resumed_by_me(QByteArray peer_id, FileTransferDialog *from);

	void send_file(QByteArray peer_id);

	void file_finished_outgoing(QByteArray peer_id);

};

#endif // FILETRANSFERCONTROLLER_H
