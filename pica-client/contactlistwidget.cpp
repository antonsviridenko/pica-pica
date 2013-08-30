#include "contactlistwidget.h"
#include "globals.h"
#include "dialogs/viewcertdialog.h"
#include "msguirouter.h"
#include <QMenu>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QInputDialog>

ContactListWidget::ContactListWidget(QWidget *parent) :
    QListWidget(parent)
{

    addcontactAct = new QAction(tr("&Add Contact..."), this);
    addcontactAct->setStatusTip(tr("add new contact"));
    connect(addcontactAct,SIGNAL(triggered()),this,SLOT(add_contact()));


    delcontactAct = new QAction(tr("&Delete Contact"),this);
    connect(delcontactAct,SIGNAL(triggered()),this,SLOT(del_contact()));

    startchatAct = new QAction(tr("Start &Chat"), this);
    connect(startchatAct, SIGNAL(triggered()), this, SLOT(start_chat()));

    viewcertAct = new QAction(tr("&View Certificate"), this);
    connect(viewcertAct, SIGNAL(triggered()), this, SLOT(view_cert()));

    connect(this, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(start_chat()));

    setContactsStorage(new Contacts(config_dbname, QByteArray((const char*)account_id, PICA_ID_SIZE)));
}

ContactListWidget::~ContactListWidget()
{
    delete storage;
}


void ContactListWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    if (this->itemAt(event->pos()))
        menu.addAction(delcontactAct);

    menu.addAction(addcontactAct);

    if (this->itemAt(event->pos()))
        menu.addAction(startchatAct);

    if (this->itemAt(event->pos()))
        menu.addAction(viewcertAct);

    menu.exec(event->globalPos());
}

void ContactListWidget::setContactsStorage(Contacts *ct)
{
    clear();
   // this->addItems(ct->GetContacts());
    storage=ct;
    contact_records=ct->GetContacts();

    for (int i=0;i<contact_records.size();i++)
    {
        addItem(QString("(%1) ").arg(contact_records[i].id.toBase64()+contact_records[i].name));
        wgitem_to_recs[this->item(i)]=&contact_records[i];
        this->item(i)->setStatusTip(contact_records[i].id.toBase64());
    }
}

void ContactListWidget::add_contact()
{
    bool ok;
    QString input=QInputDialog::getText(this,tr("Add New Contact"),tr("User ID: "),QLineEdit::Normal,NULL,&ok);

    if (ok && !input.isEmpty())
    {
        QByteArray user_id = QByteArray::fromBase64(input.toAscii()).left(PICA_ID_SIZE);

        if (user_id.size() != PICA_ID_SIZE)
        {
            QMessageBox mbx;
            mbx.setIcon(QMessageBox::Warning);
            mbx.setText(tr("Invalid user ID"));
            mbx.exec();
            return;
        }

        for(int i=0;i<contact_records.size();i++)
            if (contact_records[i].id==user_id)
            {
                QMessageBox mbx;
                mbx.setIcon(QMessageBox::Warning);
                mbx.setText(tr("User \"(%1) %2\" already exists").arg(user_id.toBase64().constData()).arg(contact_records[i].name));
                mbx.exec();
                return;
            }

        Contacts::ContactRecord r;

        r.id=user_id;
        r.name="";

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
        wgitem_to_recs[this->item(this->count()-1)]=&contact_records.last();

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

if (QMessageBox::Yes==mbx.exec())
    {
        QByteArray rm_id;
        QListWidgetItem *rm_ptr;

        storage->Delete(rm_id = wgitem_to_recs[currentItem()]->id);
        delete (rm_ptr=takeItem(currentRow()));

        for(int i=0;i<contact_records.size();i++)
        {
            if (contact_records[i].id==rm_id)
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
