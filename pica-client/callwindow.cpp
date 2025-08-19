/*
	(c) Copyright  2012 - 2020 Anton Sviridenko
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

#include "callwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

CallWindow::CallWindow(QByteArray peer_id, bool incoming)
	: m_peer_id(peer_id), is_incoming(incoming)
{
	QHBoxLayout *lh = new QHBoxLayout(this);
	QVBoxLayout *lv = new QVBoxLayout(this);

	pbAccept = new QPushButton(tr("Accept"), this);
	pbCall = new QPushButton(tr("Call"), this);
	pbHang = new QPushButton(tr("Hang Up"), this);

	lh->addWidget(pbAccept, Qt::AlignLeft);
	lh->addWidget(pbCall, Qt::AlignLeft);
	lh->addWidget(pbHang, Qt::AlignRight);
	lh->addLayout(lv);
	setLayout(lh);

	connect(pbCall, SIGNAL(clicked()), this, SLOT(call()));
	connect(pbHang, SIGNAL(clicked()), this, SLOT(hang()));
	connect(pbAccept, SIGNAL(clicked()), this, SLOT(accept()));

	if (is_incoming)
	{
		pbCall->hide();
	}
	else
	{
		pbAccept->hide();
	}
}

void CallWindow::call()
{
	emit start_call_pressed();
}

void CallWindow::hang()
{
	emit hang_call_pressed();
}

void CallWindow::accept()
{
	emit accept_call_pressed();
}
