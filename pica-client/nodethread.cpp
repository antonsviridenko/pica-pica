#include "nodethread.h"
#include "mainwindow.h"
#include "../PICA_proto.h"
#include "globals.h"
#include "dhparam.h"

QMutex NodeThread::finished_mutex;

NodeThread::NodeThread(Nodes::NodeRecord &addr, bool *completed, Accounts::AccountRecord &acc, PICA_c2n **pica_conn, QMutex *wm) :
    QThread(0)
{
    node_addr = addr;
    finished_flag = completed;
    user_account = acc;
    connection = pica_conn;
    write_mutex = wm;
    exit = 0;
    start();
}

void NodeThread::run()
{
    int ret;
    exit = 0;

    ret = PICA_new_connection(node_addr.address.toUtf8().constData(),
                              node_addr.port,
                              user_account.CA_file.toUtf8().constData(),
                              user_account.cert_file.toUtf8().constData(),
                              user_account.pkey_file.toUtf8().constData(),
                              DHParam::GetDHParamFilename().toUtf8().constData(),
                              AskPassword::ask_password_cb,
                              &ci);

    switch(ret)
    {
        case PICA_OK:

        emit NodeStatusChanged(node_addr.address, node_addr.port, true);

        finished_mutex.lock();
        if (*finished_flag)
        {
            PICA_close_connection(ci);
            exit = 1;
        }
        else
        {
            *finished_flag = true;
            *connection =  ci;
            emit ConnectedToNode(node_addr.address, node_addr.port, this);
        }
        finished_mutex.unlock();



        break;

        case PICA_ERRSOCK:
        case PICA_ERRDNSRESOLVE:

        emit NodeStatusChanged(node_addr.address, node_addr.port, false);

        exit = 1;
        break;

        case PICA_ERRSSL:
        exit = 1;
        break;

        case PICA_ERRPROTONEW:

        emit ErrorMsg(QString("Node %1:%2 has newer protocol version. Please check if update for pica-client is available")
                            .arg(node_addr.address)
                            .arg(node_addr.port)
                            );
        exit = 1;
        break;

        case PICA_ERRPROTOOLD:

        emit ErrorMsg(QString("Node %1:%2 has older protocol. Disconnected.")
                            .arg(node_addr.address)
                            .arg(node_addr.port)
                            );

        exit = 1;
        break;

        default:
        exit = 1;
    }

    if (exit)
        return;

    do
    {
        write_mutex->lock();
        if (PICA_read(ci,10000) != PICA_OK || PICA_write(ci) != PICA_OK)
            exit=1;
        write_mutex->unlock();
        msleep(300);
    }
    while (!exit);

    write_mutex->lock();
    PICA_close_connection(ci);
    *connection = NULL;
    write_mutex->unlock();
}

void NodeThread::CloseThread()
{
    exit = 1;
}

