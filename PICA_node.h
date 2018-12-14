/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef PICA_NODE_H
#define PICA_NODE_H

#ifdef WIN32

#include <winsock2.h>
#include <windows.h>

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
#include <sys/time.h>

#define SOCKET int
#define SOCKET_ERROR -1

#endif

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <time.h>
#include "PICA_nodeaddrlist.h"
#include "PICA_id.h"


#define PICA_COMM_PORT 51914
//#define PICA_DATA_PORT 51915
//#define PICA_NODE_PORT 51916

#define PICA_NEWCONN_WAITANONTLSACCEPT 1
#define PICA_NEWCONN_WAITREQUEST 2
#define PICA_NEWCONN_SENDINGOK 3
#define PICA_NEWCONN_SENDINGREJECT 4
#define PICA_NEWCONN_WAITANONTLSSHUTDOWN 5

#define PICA_CLSTATE_WAITANONTLSSHUTDOWN 1
//#define PICA_CLSTATE_SENDINGRESP 1
#define PICA_CLSTATE_TLSNEGOTIATION 2
#define PICA_CLSTATE_CONNECTED 3

//LOCAL - both clients are connected to the same node
//N2NCLR - caller client is connected to this node
//N2NCLE - callee client is connected to this node

#define PICA_CCLINK_LOCAL_WAITREP 1
#define PICA_CCLINK_LOCAL_WAITCONNCLE 2
#define PICA_CCLINK_LOCAL_WAITCONNCLEANONTLSSHUTDOWN 3
#define PICA_CCLINK_LOCAL_WAITCONNCLR 4
#define PICA_CCLINK_LOCAL_WAITCONNCLRANONTLSSHUTDOWN 5
#define PICA_CCLINK_LOCAL_ACTIVE 6

#define PICA_CCLINK_N2NCLR_WAITSEARCH 7
#define PICA_CCLINK_N2NCLR_WAITREP 8
#define PICA_CCLINK_N2NCLR_WAITCONNCLR 9
#define PICA_CCLINK_N2NCLR_WAITCONNCLRANONTLSSHUTDOWN 10
#define PICA_CCLINK_N2NCLR_ACTIVE 11

#define PICA_CCLINK_N2NCLE_WAITREP 12
#define PICA_CCLINK_N2NCLE_WAITCONNCLE 13
#define PICA_CCLINK_N2NCLE_WAITCONNCLEANONTLSSHUTDOWN 14
#define PICA_CCLINK_N2NCLE_ACTIVE 15



#define PICA_NETWORKNAME "TESTNET" // TEMP //CONF

#define NEWCONN_BUFSIZE 58 //FIXME this is the size of CONNID packet, this packet is followed by TLS session negotiation data.
//TLS data must not be interpreted as some PICA packet by PICA_processdatastream(),
//so buffer size is limited by the size of CONNID.

#define DEFAULT_BUF_SIZE 4096

struct nodelink;

struct client
{
	unsigned char id[PICA_ID_SIZE];
	struct client *up;
	struct client *left;
	struct client *right;
	SOCKET sck_comm;
	struct sockaddr_in addr;
	SSL *ssl_comm;
	unsigned char *r_buf;
	unsigned char *w_buf;
	unsigned int buflen_r;
	unsigned int buflen_w;
	unsigned int r_pos;
	unsigned int w_pos;

	unsigned int btw_ssl;//кол-во отправляемых байтов, передаваемое функции SSL_write
//Если требуется повторный вызов SSL_write,то нужно передавать те же значения аргументов,а w_pos может быть увеличен
//
	int state;
	struct client* next;//структура client может быть одновременно в составе дерева и линейного списка
	struct client* prev;

	time_t tmst;
	int disconnect_ticking;//on sending message TO client this var is set to 1, on receiving some message FROM client this var is reset to 0
};

struct cclink //структура, описывающая соединение двух клиентов между собой через сервер
{
//unsigned int cclink_id;
	unsigned int state;

	time_t tmst;
	struct client *p1;//caller
	struct client *p2;//callee

	unsigned char *caller_id;
	unsigned char *callee_id;

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

	int disconnect_ticking;
};

enum newconn_type
{
	NEWCONN_UNKNOWN,
	NEWCONN_C2N,
	NEWCONN_C2C,
	NEWCONN_N2N
};

struct newconn //структура, описывающая новое подключение (клиента)
{
	SOCKET sck;
	SSL *anonssl;
	struct sockaddr_in addr;
	time_t tmst;
	int pos;
	int state;
	enum newconn_type type;
	unsigned char buf[NEWCONN_BUFSIZE];
	union
	{
		struct client *cl;
		struct cclink *cc;
	} iconn;
};

struct nodelink
{
	struct sockaddr_in addr;
	SOCKET sck;
	SSL *anonssl;

	unsigned int addr_type;//тип адреса узла - PICA_PROTO_NEWNODE_IPV4,PICA_PROTE_NEWNODE_IPV6,...
	void *node_addr;//указатель на структуру адреса {PICA_nodeaddr_ipv4, PICA_nodeaddr_ipv6,PICA_nodeaddr_dns,..}

	unsigned char *r_buf;
	unsigned char *w_buf;
	unsigned int buflen_r;
	unsigned int buflen_w;
	unsigned int r_pos;
	unsigned int w_pos;
	unsigned int btw_ssl;

	struct nodelink *next;
	struct nodelink *prev;

	time_t tmst;
	int disconnect_ticking;
};

extern SSL_CTX *anon_ctx;

#endif
