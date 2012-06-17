#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QComboBox>
#include <QSystemTrayIcon>

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

protected:

private:
    void createMenus();

    Ui::MainWindow *ui;
    ContactListWidget *contact_list;
    QComboBox *status;

    QMenu *contactsMenu;
    QMenu *helpMenu;
    QMenu *nodesMenu;

    bool status_change_disable_flag;
    void SetStatus(bool connected);

private slots:
    void set_online();
    void set_offline();
    void status_changed(int index);

};

#endif // MAINWINDOW_H
