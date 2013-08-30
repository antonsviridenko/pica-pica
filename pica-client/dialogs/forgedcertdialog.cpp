#include "forgedcertdialog.h"
#include "viewcertdialog.h"
#include <QGridLayout>
#include <QLabel>

ForgedCertDialog::ForgedCertDialog(QByteArray peer_id, QString received_cert, QString stored_cert, QWidget *parent) :
    QDialog(parent),
    received_cert_(received_cert),
    stored_cert_(stored_cert)
{
    QGridLayout *grid = new QGridLayout(this);

    QLabel *scary_text = new QLabel(tr(
"Previously stored and currently present certificates for user %1 do not match.\n\
This means that probably someone is trying to eavesdrop your communication\n\
using man-in-the-middle attack or impersonate the user.\n\
Connection aborted.").arg(peer_id.toBase64().constData()));

    btOK = new QPushButton(tr("OK"));
    btShow_stored_cert = new QPushButton(tr("View Stored Certificate..."));
    btShow_received_cert = new QPushButton(tr("View Received Certificate..."));

    grid->addWidget(scary_text, 0,0, 1, 3, Qt::AlignCenter);
    grid->addWidget(btShow_stored_cert, 1, 0, Qt::AlignCenter);
    grid->addWidget(btShow_received_cert, 1, 2, Qt::AlignCenter);
    grid->addWidget(btOK, 2, 1, Qt::AlignCenter);


    setLayout(grid);

    connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));
    connect(btShow_stored_cert, SIGNAL(clicked()), this, SLOT(StoredCert()));
    connect(btShow_received_cert, SIGNAL(clicked()), this, SLOT(ReceivedCert()));
}

void ForgedCertDialog::OK()
{
    done(0);
}

void ForgedCertDialog::ReceivedCert()
{
    ViewCertDialog vcd;
    vcd.SetCert(received_cert_);
    vcd.exec();
}

void ForgedCertDialog::StoredCert()
{
    ViewCertDialog vcd;
    vcd.SetCert(stored_cert_);
    vcd.exec();
}
