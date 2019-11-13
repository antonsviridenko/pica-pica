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
#include "contactlistwidget.h"
#include "globals.h"
#include "dialogs/viewcertdialog.h"
#include "dialogs/showpicaiddialog.h"
#include "dialogs/filetransferdialog.h"
#include "msguirouter.h"
#include "filetransfercontroller.h"
#include <QMenu>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QTimer>

ContactListWidget::ContactListWidget(QWidget *parent) :
	QListWidget(parent)
{

	addcontactAct = new QAction(tr("&Add Contact..."), this);
	addcontactAct->setStatusTip(tr("add new contact"));
	connect(addcontactAct, SIGNAL(triggered()), this, SLOT(add_contact()));


	delcontactAct = new QAction(tr("&Delete Contact"), this);
	connect(delcontactAct, SIGNAL(triggered()), this, SLOT(del_contact()));

	startchatAct = new QAction(tr("Start &Chat"), this);
	connect(startchatAct, SIGNAL(triggered()), this, SLOT(start_chat()));

	viewcertAct = new QAction(tr("&View Certificate"), this);
	connect(viewcertAct, SIGNAL(triggered()), this, SLOT(view_cert()));

	showidAct = new QAction(tr("Show Pica Pica &ID"), this);
	connect(showidAct, SIGNAL(triggered()), this, SLOT(show_id()));

	sendfileAct = new QAction(tr("Send &File"), this);
	connect(sendfileAct, SIGNAL(triggered()), this, SLOT(send_file()));

	showblacklistAct = new QAction(tr("Show &Blacklisted Contacts"), this);
	showblacklistAct->setCheckable(true);
	connect(showblacklistAct, SIGNAL(triggered(bool)), this, SLOT(show_blacklist(bool)));

	movetoblacklistAct = new QAction(tr("&Move to Blacklist"), this);
	connect(movetoblacklistAct, SIGNAL(triggered()), this, SLOT(move_to_blacklist()));

	removefromblacklistAct = new QAction(tr("&Remove From Blacklist"), this);
	connect(removefromblacklistAct, SIGNAL(triggered()), this, SLOT(remove_from_blacklist()));

	connect(this, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(start_chat()));

	setContactsStorage(new Contacts(config_dbname, Accounts::GetCurrentAccount().id));
}

ContactListWidget::~ContactListWidget()
{
	delete storage;
}


void ContactListWidget::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);

	if (this->itemAt(event->pos()) && wgitem_to_recs.contains(this->itemAt(event->pos())))
		menu.addAction(delcontactAct);

	menu.addAction(addcontactAct);

	if (this->itemAt(event->pos()) && wgitem_to_recs.contains(this->itemAt(event->pos())))
	{
		menu.addAction(startchatAct);
		menu.addAction(viewcertAct);
		menu.addAction(showidAct);
		menu.addAction(sendfileAct);

		if (wgitem_to_recs[this->itemAt(event->pos())]->type == Contacts::blacklisted)
		{
			menu.addAction(removefromblacklistAct);
		}
		else
		{
			menu.addAction(movetoblacklistAct);
		}
	}
	menu.exec(event->globalPos());
}

void ContactListWidget::setContactsStorage(Contacts *ct, bool show_blacklisted)
{
	clear();
	wgitem_to_recs.clear();
	// this->addItems(ct->GetContacts());
	storage = ct;
	contact_records = ct->GetContacts();

	for (int i = 0; i < contact_records.size(); i++)
	{
		addItem(QString("(%1...) %2").arg(contact_records[i].id.toBase64().left(8).constData()).arg(contact_records[i].name));
		wgitem_to_recs[this->item(i)] = &contact_records[i];
		this->item(i)->setStatusTip(contact_records[i].id.toBase64());
		this->item(i)->setToolTip(contact_records[i].id.toBase64().constData());
	}

	if (show_blacklisted)
	{
		QList<Contacts::ContactRecord> bl = ct->GetContacts(Contacts::blacklisted);
		int offset = contact_records.size();

		if(!bl.empty())
		{
			addItem(QString("--Blacklisted Contacts--"));
			offset++;
			contact_records.append(bl);
		}

		for (int i = 0; i < bl.size(); i++)
		{
			addItem(QString("(%1...) %2").arg(bl[i].id.toBase64().left(8).constData()).arg(bl[i].name));
			wgitem_to_recs[this->item(i + offset)] = &contact_records[i + offset - 1];
			this->item(i + offset)->setStatusTip(bl[i].id.toBase64());
			this->item(i + offset)->setToolTip(bl[i].id.toBase64().constData());
		}
	}
}

void ContactListWidget::add_contact()
{
	bool ok;
	QString input = QInputDialog::getText(this, tr("Add New Contact"),
			tr("<b>It is very important to make sure this ID actually belongs to the person you want to talk to.</b><br>Pica Pica ID: "), QLineEdit::Normal, NULL, &ok);

	if (ok && !input.isEmpty())
	{
		QByteArray user_id = QByteArray::fromBase64(input.toLatin1()).left(PICA_ID_SIZE);

		if (user_id.size() != PICA_ID_SIZE)
		{
			QMessageBox mbx;
			mbx.setIcon(QMessageBox::Warning);
			mbx.setText(tr("Invalid user ID"));
			mbx.exec();
			return;
		}

		for(int i = 0; i < contact_records.size(); i++)
			if (contact_records[i].id == user_id)
			{
				QMessageBox mbx;
				mbx.setIcon(QMessageBox::Warning);
				mbx.setText(tr("User \"(%1) %2\" already exists").arg(user_id.toBase64().constData()).arg(contact_records[i].name));
				mbx.exec();
				return;
			}

		Contacts::ContactRecord r;

		r.id = user_id;
		r.name = "";

		storage->Add(user_id);

		if (!storage->isOK())
		{
			QMessageBox mbx;
			mbx.setIcon(QMessageBox::Warning);
			mbx.setText(storage->GetLastError());
			mbx.exec();
			return;
		}

		addItem(QString("(%1) ").arg(user_id.toBase64().constData()));
		contact_records.append(r);
		wgitem_to_recs[this->item(this->count() - 1)] = &contact_records.last();

	}

}

void ContactListWidget::del_contact()
{
	QMessageBox mbx;
	mbx.setText(QString(tr("Are you sure you want to remove user \"%1\" from your contact list?")).arg(currentItem()->text()));
	mbx.setIcon(QMessageBox::Question);
	mbx.addButton(QMessageBox::Yes);
	mbx.addButton(QMessageBox::No);
	mbx.setDefaultButton(QMessageBox::No);

	if (QMessageBox::Yes == mbx.exec())
	{
		QByteArray rm_id;
		QListWidgetItem *rm_ptr;

		storage->Delete(rm_id = wgitem_to_recs[currentItem()]->id);
		delete (rm_ptr = takeItem(currentRow()));

		for(int i = 0; i < contact_records.size(); i++)
		{
			if (contact_records[i].id == rm_id)
			{
				contact_records.removeAt(i);
				break;
			}
		}

		wgitem_to_recs.remove(rm_ptr);
	}

}

void ContactListWidget::start_chat()
{
	msguirouter->start_chat(wgitem_to_recs[currentItem()]->id);
}

void ContactListWidget::view_cert()
{
	QString cert_pem = storage->GetContactCert(wgitem_to_recs[currentItem()]->id);

	if (!storage->isOK())
	{
		QMessageBox mbx;
		mbx.setText(storage->GetLastError());
		mbx.exec();
	}

	if (!cert_pem.isEmpty())
	{
		ViewCertDialog vcd;
		vcd.SetCert(cert_pem);
		vcd.exec();
	}
	else
	{
		QMessageBox mbx;
		mbx.setText(tr("Certificate is not stored for this contact yet"));
		mbx.exec();
	}
}

void ContactListWidget::show_id()
{
	ShowPicaIdDialog d(wgitem_to_recs[currentItem()]->name,
	                   wgitem_to_recs[currentItem()]->id,
	                   tr("Contact's ID"));
	d.exec();
}

void ContactListWidget::Reload()
{
	setContactsStorage(storage);
}

void ContactListWidget::show_blacklist(bool checked)
{
	setContactsStorage(storage, checked);
}

void ContactListWidget::move_to_blacklist()
{
	storage->SetContactType(wgitem_to_recs[currentItem()]->id, Contacts::blacklisted);
	setContactsStorage(storage, showblacklistAct->isChecked());
}

void ContactListWidget::remove_from_blacklist()
{
	storage->SetContactType(wgitem_to_recs[currentItem()]->id, Contacts::regular);
	setContactsStorage(storage, showblacklistAct->isChecked());
}

void ContactListWidget::send_file()
{
	ftctrl->send_file(wgitem_to_recs[currentItem()]->id);
}
