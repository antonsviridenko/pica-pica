#ifndef PICAACTIONCENTER_H
#define PICAACTIONCENTER_H

#include <QObject>
#include <QAction>
#include <QLabel>

class PicaActionCenter : public QObject
{
    Q_OBJECT
public:
    explicit PicaActionCenter(QObject *parent = 0);

    QAction *AboutAct() {return aboutAct;};
    QAction *ExitAct() {return exitAct;};
    
private:
    QAction *aboutAct;
    QAction *exitAct;
    QLabel *link;
    bool eventFilter(QObject *obj, QEvent *ev);

signals:
    
public slots:
private slots:
    void about();
    void exit();
};

#endif // PICAACTIONCENTER_H
