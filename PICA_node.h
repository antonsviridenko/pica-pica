#ifndef PICA_NODE_H
#define PICA_NODE_H

#ifdef WIN32

#include <windows.h>
#include <winsock2.h>

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/errno.h>

#define SOCKET int
#define SOCKET_ERROR -1

#endif

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <time.h>
#include "PICA_nodeaddrlist.h"


#define PICA_COMM_PORT 51914
//#define PICA_DATA_PORT 51915
//#define PICA_NODE_PORT 51916

#define PICA_CLSTATE_SENDINGRESP 1
#define PICA_CLSTATE_TLSNEGOTIATION 2
#define PICA_CLSTATE_CONNECTED 3

//LOCAL - оба клиента подсоединены к одному узлу
//N2NCLR - к узлу подключен вызывающий клиент
//N2NCLE - к узлу подключен вызываемый клиент

#define PICA_CCLINK_LOCAL_WAITREP 1
#define PICA_CCLINK_LOCAL_WAITCONNCLE 2
#define PICA_CCLINK_LOCAL_WAITCONNCLR 3
#define PICA_CCLINK_LOCAL_ACTIVE 4

#define PICA_CCLINK_N2NCLR_WAITSEARCH 5
#define PICA_CCLINK_N2NCLR_WAITREP 6
#define PICA_CCLINK_N2NCLR_WAITCONNCLR 7
#define PICA_CCLINK_N2NCLR_ACTIVE 8

#define PICA_CCLINK_N2NCLE_WAITREP 9
#define PICA_CCLINK_N2NCLE_WAITCONNCLE 10
#define PICA_CCLINK_N2NCLE_ACTIVE 11



#define PICA_NETWORKNAME "TESTNET" // TEMP //CONF

#define NEWCONN_BUFSIZE 10
#define CL_RBUFSIZE 32
#define CL_WBUFSIZE 128

#define DEFAULT_BUF_SIZE 4096

struct nodelink;

struct client
{
unsigned int id;
struct client *up;
struct client *left;
struct client *right;
SOCKET sck_comm;
struct sockaddr_in addr;
SSL *ssl_comm;
unsigned char r_buf[CL_RBUFSIZE];
unsigned char w_buf[CL_WBUFSIZE];
unsigned int r_pos;
unsigned int w_pos;

unsigned int btw_ssl;//кол-во отправляемых байтов, передаваемое функции SSL_write
//Если требуется повторный вызов SSL_write,то нужно передавать те же значения аргументов,а w_pos может быть увеличен
//
int state;
struct client* next;//структура client может быть одновременно в составе дерева и линейного списка
struct client* prev;



};

struct cclink //структура, описывающая соединение двух клиентов между собой через сервер
{
//unsigned int cclink_id;
unsigned int state;

clock_t tmst;
struct client *p1;//caller
struct client *p2;//callee

unsigned int caller_id;
unsigned int callee_id;

struct nodelink *caller_node;
struct nodelink *callee_node;

SOCKET sck_p1;
SOCKET sck_p2;
int jam_p1p2;
int jam_p2p1;
char* buf_p1p2;
char* buf_p2p1;
unsigned int buflen_p1p2;
unsigned int buflen_p2p1;
unsigned int bufpos_p1p2;
unsigned int bufpos_p2p1;	
struct cclink *next;
struct cclink *prev;

};

struct newconn //структура, описывающая новое подключение (клиента)
{
SOCKET sck;
struct sockaddr_in addr;
clock_t tmst;
int pos;
unsigned char buf[NEWCONN_BUFSIZE];
};

struct nodelink
{
struct sockaddr_in addr;
SOCKET sck;
SSL *ssl;

unsigned int addr_type;//тип адреса узла - PICA_PROTO_NEWNODE_IPV4,PICA_PROTE_NEWNODE_IPV6,...
void *node_addr;//указатель на структуру адреса {PICA_nodeaddr_ipv4, PICA_nodeaddr_ipv6,PICA_nodeaddr_dns,..}

unsigned char *r_buf;
unsigned char *w_buf;
unsigned int buflen_r;
unsigned int buflen_w;
unsigned int r_pos;
unsigned int w_pos;

struct nodelink *next;
struct nodelink *prev;
};

#endif
