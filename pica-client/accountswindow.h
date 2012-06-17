#ifndef ACCOUNTSWINDOW_H
#define ACCOUNTSWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include "accounts.h"
#include <QList>

class AccountsWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AccountsWindow(QWidget *parent = 0);

private:
    QComboBox *cb_accounts;
    QPushButton *bt_login;
    QPushButton *bt_registernew;
    QPushButton *bt_addimport;
    QPushButton *bt_delete;

    Accounts accounts;
    QList<Accounts::AccountRecord> L;

    void LoadAccounts();

    void closeEvent(QCloseEvent *);


signals:

public slots:

private slots:
    void login_click();
    void register_click();
    void add_click();
    void delete_click();

};

#endif // ACCOUNTSWINDOW_H
