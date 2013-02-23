#ifndef PICAACTIONCENTER_H
#define PICAACTIONCENTER_H

#include <QObject>
#include <QAction>

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

signals:
    
public slots:
private slots:
    void about();
    void exit();
};

#endif // PICAACTIONCENTER_H
