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
#include "nodesdialog.h"
#include "../nodes.h"
#include "../globals.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>

NodesDialog::NodesDialog(QWidget *parent) :
	QDialog(parent)
{
	Nodes nodes(config_dbname);
	lwNodes = new QListWidget(this);
	pbOk = new QPushButton(tr("OK"), this);

	QHBoxLayout *lh = new QHBoxLayout(this);
	QVBoxLayout *lv = new QVBoxLayout();

	lh->addWidget(lwNodes, 4, Qt::AlignLeft);
	lv->addWidget(pbOk, 0, Qt::AlignTop);
	lh->addLayout(lv, 0);

	setLayout(lh);

	QList<Nodes::NodeRecord> noderecords = nodes.GetNodes();

	while(!noderecords.empty())
	{
		Nodes::NodeRecord nr = noderecords.takeFirst();
		new QListWidgetItem(nr.address.append(':').append(QString::number(nr.port)), lwNodes);
	}

	connect(pbOk, SIGNAL(clicked()), this, SLOT(OK()));

	setWindowTitle(tr("Known nodes"));
}

void NodesDialog::OK()
{
	done(0);
}
