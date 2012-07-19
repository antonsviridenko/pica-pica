#include "registeraccountdialog.h"
#include <QVBoxLayout>

RegisterAccountDialog::RegisterAccountDialog(QWidget *parent) :
    QDialog(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *lbNickname = new QLabel(tr("Name:"));
    nickname = new QLineEdit();
    btRegister = new QPushButton(tr("Register"));
    lbStatus = new QLabel();
    btOK = new QPushButton(tr("OK"));

    layout->addWidget(lbNickname);
    layout->addWidget(nickname);
    layout->addWidget(btRegister);
    layout->addWidget(lbStatus);
    layout->addWidget(btOK);

    setLayout(layout);

    connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));
    connect(btRegister, SIGNAL(clicked()), this, SLOT(Register()));
}

void RegisterAccountDialog::OK()
{
    done(0);
}

void RegisterAccountDialog::Register()
{

}
