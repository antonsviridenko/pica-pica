#ifndef NODETHREAD_H
#define NODETHREAD_H

#include <QThread>
#include <QMutex>
#include "nodes.h"
#include "../PICA_client.h"
#include "accounts.h"


class NodeThread : public QThread
{
    Q_OBJECT
public:
    explicit NodeThread( Nodes::NodeRecord &addr, bool *completed,Accounts::AccountRecord &acc,struct PICA_conninfo **pica_conn, QMutex *wm);
    void CloseThread();
private:
    void run();
    static QMutex finished_mutex;

    QMutex *write_mutex;

    bool *finished_flag;
    Nodes::NodeRecord  node_addr;
    struct PICA_conninfo *ci;
    Accounts::AccountRecord user_account;
    struct PICA_conninfo **connection;

    int exit;


signals:
    void NodeStatusChanged(QString addr, quint16 port, bool alive);
    void ConnectedToNode(QString addr, quint16 port, NodeThread *thread);

public slots:

};

#endif // NODETHREAD_H
