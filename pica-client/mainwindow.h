#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QComboBox>
#include <QSystemTrayIcon>
#include <QPushButton>

#include "contacts.h"
#include "contactlistwidget.h"
#include "askpassword.h"

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void AddNotification(QString &text);
protected:

private:
    void createMenus();

    Ui::MainWindow *ui;
    ContactListWidget *contact_list;
    QComboBox *status;
    QListWidget *notifications;
    QPushButton *bt_showhide_notifications;

    QMenu *contactsMenu;
    QMenu *helpMenu;
    QMenu *nodesMenu;

    bool status_change_disable_flag;
    void SetStatus(bool connected);
    bool status_notifications_hidden;

private slots:
    void set_online();
    void set_offline();
    void status_changed(int index);
    void show_notifications();
    void hide_notifications();
    void showhide_click();

};

#endif // MAINWINDOW_H
