#ifndef SHOWPICAIDDIALOG_H
#define SHOWPICAIDDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include <QByteArray>

class ShowPicaIdDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShowPicaIdDialog(QString name, QByteArray id, QString caption, QWidget *parent = 0);

private slots:
    void Ok();

private:
    QLabel *lbname_;
    QLineEdit *picaid_;
    QPushButton *btok_;

};

#endif // SHOWPICAIDDIALOG_H
