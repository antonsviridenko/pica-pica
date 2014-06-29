#include "filetransferdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <stdlib.h>

FileTransferDialog::FileTransferDialog(QByteArray peer_id, QString filename, quint64 size,
                                        TransferDirection drct, QWidget *parent) :
    peer_id_(peer_id),
    QDialog(parent),
    dir_(drct),
    filename_(filename),
    filesize_(size)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    QHBoxLayout *btlayout = new QHBoxLayout();

    lbFilename = new QLabel(this);
    lbProgressStatus = new QLabel(this);
    lbTransferSpeed = new QLabel(this);
    lbRemainingTime = new QLabel(this);
    lbTransferStatus = new QLabel(this);

    pgbar = new QProgressBar(this);
    leftbutton = new QPushButton(this);
    rightbutton = new QPushButton(this);

    layout->addWidget(lbFilename);
    layout->addWidget(lbProgressStatus);
    layout->addWidget(lbTransferSpeed);
    layout->addWidget(lbRemainingTime);
    layout->addWidget(pgbar);
    layout->addWidget(lbTransferStatus);
    layout->addLayout(btlayout);

    btlayout->addWidget(leftbutton);
    btlayout->addWidget(rightbutton);

    if (drct == RECEIVING)
    {
        leftbutton->setText(tr("Accept"));
        rightbutton->setText(tr("Deny"));

        setWindowTitle(tr("Receiving file %1").arg(filename_));
    }
    else if (drct == SENDING)
    {
        leftbutton->setText(tr("Pause"));
        rightbutton->setText(tr("Cancel"));
        leftbutton->setEnabled(false);

        setWindowTitle(tr("Sending file %1").arg(filename_));
    }
    setTransferStatus(WAITINGFORACCEPT);

    lbFilename->setText(filename_);

    pgbar->setMinimum(0);
    pgbar->setMaximum(100);

    update(0);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()));
    timer->start(1000);
    progress_ = 0;
    prevprogress_ = 0;

    connect(leftbutton, SIGNAL(clicked()), this, SLOT(leftbuttonclick()));
    connect(rightbutton, SIGNAL(clicked()), this, SLOT(rightbuttonclick()));
}

void FileTransferDialog::update(quint64 progress)
{
    progress_ = progress;
    percents = (double)progress / filesize_ * 100.0;

    lbProgressStatus->setText(bytestoHumanBase2(progress) + " / "
                     + bytestoHumanBase2(filesize_) + QString(" %1 %").arg((int)percents));

    pgbar->setValue(percents);

    if (progress == filesize_)
    {
        setTransferStatus(FINISHED);
    }
}

void FileTransferDialog::setTransferStatus(TransferStatus st)
{
    bool isTerminalState = false;

    switch(st)
    {
    case WAITINGFORACCEPT:
    if (dir_ == SENDING)
        lbTransferStatus->setText(tr("Waiting for peer to accept the file"));
    else
        lbTransferStatus->setText(tr("Waiting for acceptance"));
    break;

    case DENIED:
    lbTransferStatus->setText(tr("Denied by peer"));
    isTerminalState = true;
    break;

    case SENDINGFILE:
    leftbutton->setEnabled(true);
    lbTransferStatus->setText(tr("Sending file"));
    break;

    case RECEIVINGFILE:
    lbTransferStatus->setText(tr("Receiving file"));
    break;

    case PAUSED:
    lbTransferStatus->setText(tr("Paused"));
    break;

    case CANCELLED:
    lbTransferStatus->setText(tr("Cancelled"));
    isTerminalState = true;
    break;

    case FINISHED:
    lbTransferStatus->setText(tr("Finished"));
    isTerminalState = true;
    break;
    }

    if (isTerminalState)
    {
        leftbutton->setEnabled(false);
        rightbutton->setEnabled(false);

        QWidget::setAttribute(Qt::WA_DeleteOnClose);
        QWidget::setAttribute(Qt::WA_QuitOnClose, false);
    }
}

void FileTransferDialog::pausedByPeer()
{
    leftbutton->setEnabled(false);
    setTransferStatus(PAUSED);
}

void FileTransferDialog::resumedByPeer()
{
    leftbutton->setEnabled(true);
    setTransferStatus( dir_ == SENDING ? SENDINGFILE : RECEIVINGFILE);
}

void FileTransferDialog::cancelledByPeer()
{
    setTransferStatus(CANCELLED);
}

void FileTransferDialog::leftbuttonclick()
{
    if (dir_ == RECEIVING)
    {
        if (leftbutton->text() == tr("Accept"))
        {
            leftbutton->setText("Pause");
            rightbutton->setText("Cancel");
            emit acceptedFile(peer_id_);
            return;
        }
    }

    if (leftbutton->text() == tr("Pause"))
    {
        leftbutton->setText(tr("Resume"));
        emit pausedFile(peer_id_, this);
        setTransferStatus(PAUSED);
    }
    else if (leftbutton->text() == tr("Resume"))
    {
        leftbutton->setText(tr("Pause"));
        emit resumedFile(peer_id_, this);
        setTransferStatus(dir_ == SENDING ? SENDINGFILE : RECEIVINGFILE);
    }
}

void FileTransferDialog::rightbuttonclick()
{
    if (dir_ == RECEIVING)
    {
        if (rightbutton->text() == tr("Deny"))
        {
            emit deniedFile(peer_id_);
            return;
        }
    }

    if (rightbutton->text() == tr("Cancel"))
    {
        emit cancelledFile(peer_id_, this);
    }
}

QString FileTransferDialog::bytestoHumanBase2(quint64 bytes)
{
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int i = 0;
    double size = bytes;

    while (size > 1024)
    {
        size /= 1024;
        i++;
    }

    return QString("%1 %2").arg(size, 6, 'f', i ? 2 : 0).arg(units[i]);
}

QString FileTransferDialog::bytestoHumanBase10(quint64 bytes)
{
    const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
    int i = 0;
    double size = bytes;

    while (size > 1000)
    {
        size /= 1000;
        i++;
    }

    return QString("%1 %2").arg(size, 6, 'f', i ? 2 : 0).arg(units[i]);
}

QString FileTransferDialog::timeLeft(quint64 seconds)
{
    if (seconds < 60)
    {
        return QString(tr("%1 seconds")).arg(seconds);
    }

    int minutes = seconds/60;
    int hours = minutes/60;
    double days = (double)hours/24.0;

    if (days > 1.0 )
        return QString(tr("%1 days")).arg(days, 6, 'f', 1);

    if (hours > 0)
        return QString("%1h:%2m").arg(hours).arg(minutes % 60);

    return QString("%1:%2").arg(minutes).arg((double)(seconds % 60), 2, 'f', 0, '0');
}

void FileTransferDialog::timeout()
{
    if (progress_ >= filesize_)
        return;

    quint64 bps = progress_ - prevprogress_;
    prevprogress_ = progress_;

    if (bps != 0)
    {
        quint64 secondsleft = (filesize_ - progress_)/bps;

        lbRemainingTime->setText(timeLeft(secondsleft));
    }
    else
        lbRemainingTime->setText(QString());

    lbTransferSpeed->setText(bytestoHumanBase10(bps) + tr("/s"));
}
