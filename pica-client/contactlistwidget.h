#ifndef CONTACTLISTWIDGET_H
#define CONTACTLISTWIDGET_H

#include <QListWidget>
#include "contacts.h"
#include <QAction>

#include "chatwindow.h" //debug remove

class ContactListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ContactListWidget(QWidget *parent = 0);
    ~ContactListWidget();

    QAction *addcontactAct;
    QAction *delcontactAct;
    QAction *startchatAct;
    QAction *viewcertAct;
protected:
    void contextMenuEvent(QContextMenuEvent *event);

private:
    void setContactsStorage(Contacts *ct);
    Contacts* storage;
    QList<Contacts::ContactRecord> contact_records;
    QMap<QListWidgetItem*,Contacts::ContactRecord*> wgitem_to_recs;

    //ChatWindow *cw_debug_remove;

signals:

private slots:
    void add_contact();
    void del_contact();
    void start_chat();
    void view_cert();

    //void debug_onmsginput(QString msg, ChatWindow *sender_window);//debug remove after debug
};

#endif // CONTACTLISTWIDGET_H
