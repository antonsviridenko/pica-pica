#ifndef FILETRANSFERDIALOG_H
#define FILETRANSFERDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class FileTransferDialog : public QDialog
{
    Q_OBJECT
public:
    enum TransferDirection
    {
    SENDING,
    RECEIVING
    };

    explicit FileTransferDialog(QString filename, quint64 size, TransferDirection drct, QWidget *parent = 0);



private:
    QLabel *lbFilename;
    QLabel *lbProgressStatus;
    QLabel *lbTransferSpeed;
    QLabel *lbRemainingTime;
    QProgressBar *pgbar;
    QPushButton *leftbutton;
    QPushButton *rightbutton;

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
    void pausedFile();
    void resumedFile();
    void cancelledFile();
    void acceptedFile();
    void deniedFile();

public slots:
    void update(quint64 progress);

private slots:
    void timeout();
    void leftbuttonclick();
    void rightbuttonclick();

};

#endif // FILETRANSFERDIALOG_H
