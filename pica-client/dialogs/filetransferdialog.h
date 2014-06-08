#ifndef FILETRANSFERDIALOG_H
#define FILETRANSFERDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QByteArray>

class FileTransferDialog : public QDialog
{
    Q_OBJECT
public:
    enum TransferDirection
    {
    SENDING,
    RECEIVING
    };

    enum TransferStatus
    {
    WAITINGFORACCEPT,
    DENIED,
    SENDINGFILE,
    RECEIVINGFILE,
    PAUSED,
    CANCELLED,
    FINISHED
    };

    explicit FileTransferDialog(QByteArray peer_id, QString filename, quint64 size,
                                TransferDirection drct, QWidget *parent = 0);

    TransferDirection getTransferDirection() {return dir_;};


private:
    QLabel *lbFilename;
    QLabel *lbProgressStatus;
    QLabel *lbTransferSpeed;
    QLabel *lbRemainingTime;
    QLabel *lbTransferStatus;
    QProgressBar *pgbar;
    QPushButton *leftbutton;
    QPushButton *rightbutton;

    QByteArray peer_id_;
    TransferDirection dir_;
    QString filename_;
    quint64 filesize_;

    double percents;
    quint64 progress_;
    quint64 prevprogress_;

    static QString bytestoHumanBase2(quint64 bytes);
    static QString bytestoHumanBase10(quint64 bytes);
    static QString timeLeft(quint64 seconds);

    QTimer *timer;
signals:
    void pausedFile(QByteArray peer_id, FileTransferDialog *sender);
    void resumedFile(QByteArray peer_id, FileTransferDialog *sender);
    void cancelledFile(QByteArray peer_id, FileTransferDialog *sender);
    void acceptedFile(QByteArray peer_id);
    void deniedFile(QByteArray peer_id);

public slots:
    void update(quint64 progress);
    void setTransferStatus(enum TransferStatus st);

private slots:
    void timeout();
    void leftbuttonclick();
    void rightbuttonclick();

};

#endif // FILETRANSFERDIALOG_H
