#include "viewcertdialog.h"
#include "openssltool.h"
#include <QVBoxLayout>
#include <QMessageBox>

#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <QFile>

ViewCertDialog::ViewCertDialog(QWidget *parent) :
    QDialog(parent)
{
    QVBoxLayout *layout;

    layout = new QVBoxLayout;
    cert_text = new QTextEdit(this);
    btOK = new QPushButton(tr("OK"));
    btOK->setMaximumWidth(45);
    this->setMinimumWidth(640);
    this->setMinimumHeight(480);

    QTextCharFormat fmt;

    fmt = cert_text->currentCharFormat();
    fmt.setFontFixedPitch(true);
    fmt.setFontStyleHint(QFont::Monospace);
    fmt.setFontFamily("Liberation Mono");
    cert_text->setCurrentCharFormat(fmt);

    cert_text->setReadOnly(true);

    layout->addWidget(cert_text);
    layout->addWidget(btOK, 0, Qt::AlignCenter);

    setLayout(layout);

    connect(btOK, SIGNAL(clicked()), this, SLOT(OK()));

}

void ViewCertDialog::SetCert(QString cert_pem)
{
    cert_text->setPlainText(OpenSSLTool::CertTextFromString(cert_pem));
}

void ViewCertDialog::OK()
{
    done(0);
}
