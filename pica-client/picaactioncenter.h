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
    QAction *ShowMyIDAct() {return showmyidAct;};
    QAction *MuteSoundsAct() {return muteSoundsAct;};
    
private:
    QAction *aboutAct;
    QAction *exitAct;
    QAction *showmyidAct;
    QAction *muteSoundsAct;

signals:
    
public slots:
private slots:
    void about();
    void exit();
    void showmyid();
    void muteSounds(bool yes);
};

#endif // PICAACTIONCENTER_H
