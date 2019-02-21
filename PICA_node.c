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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <signal.h>
#include "PICA_security.h"
#include "PICA_node.h"
#include "PICA_proto.h"
#include "PICA_msgproc.h"
#include "PICA_nodeaddrlist.h"
#include "PICA_netconf.h"
#include "PICA_common.h"
#include "PICA_nodeconfig.h"
#include "PICA_log.h"
#include "PICA_nodewait.h"
#include "PICA_signverify.h"

#ifdef NO_RAND_DEV
#include "PICA_rand_seed.h"
#endif

#ifdef WIN32
typedef u_long in_addr_t;
typedef u_short in_port_t;
typedef u_short uint16_t;
#endif

#define MAX_NEWCONNS 64

#define NEWCONN_TIMEOUT 10 //sec
#define SKYNET_REFRESH_TIMEOUT 93
#define SELECT_TIMEOUT 1

#define NOREPLY_TIMEOUT 15
#define KEEPALIVE_TIMEOUT 60

time_t skynet_refresh_tmst;

clock_t TMO_CCLINK_WAITACTIVE = 15; //CONF

char *my_addr;//TEMP CONF FIXME

SOCKET listen_comm_sck;//*

SSL_CTX *ctx;
SSL_CTX *anon_ctx;
struct newconn newconns[MAX_NEWCONNS];
unsigned int newconns_pos = 0;


struct client *client_tree_root;
struct client *client_list_head;
struct client *client_list_end;

struct cclink *cclink_list_head;
struct cclink *cclink_list_end;

unsigned int cclink_list_count;

struct nodelink *nodelink_list_head;
struct nodelink *nodelink_list_end;
unsigned int nodelink_list_count;


const char msg_INITRESPOK[] = {PICA_PROTO_INITRESPOK, PICA_PROTO_INITRESPOK, 'O', 'K'};
const char msg_VERDIFFER[] = {PICA_PROTO_VERDIFFER, PICA_PROTO_VERDIFFER, PICA_PROTO_VER_HIGH, PICA_PROTO_VER_LOW};

unsigned int procmsg_INITREQ(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_CONNID(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_NODECONNREQ(unsigned char* buf, unsigned int size, void* ptr);

unsigned int procmsg_CONNREQOUTG(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_CONNALLOW(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_CONNDENY(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_NODERESP(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_NEWNODE(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_NODELISTREQ(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_NODELIST(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_N2NFOUND(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_SEARCH(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_N2NCONNREQOUTG(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_N2NALLOW(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_N2NNOTFOUND(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_N2NMSG(unsigned char* buf, unsigned int size, void* ptr);

unsigned int procmsg_PINGREQ_client(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_PINGREP_client(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_PINGREQ_node(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_PINGREP_node(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_dummy_multi_c2n(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_MULTILOGIN_client(unsigned char* buf, unsigned int size, void* ptr);
unsigned int procmsg_MULTILOGIN_node(unsigned char* buf, unsigned int size, void* ptr);

struct cclink* cclink_list_add(struct client *clr, struct client *cle);
struct cclink* cclink_list_search(const unsigned char *caller_id, const unsigned char *callee_id);
struct cclink* cclink_list_findwaitsearch(const unsigned char *callee_id);
void cclink_list_addlocal(struct client *clr, struct client *cle);
void cclink_list_delete(struct cclink *l);
void cclink_list_addn2nclr(struct client *clr, const unsigned char *callee_id);
void cclink_setwaitconn(struct cclink *ccl);
void cclink_list_addn2ncle(const unsigned char *caller_id, struct nodelink *caller_node, struct client *cle);
void cclink_activate(struct cclink *ccl);
void cclink_setwaitanontlsshutdown(struct cclink *link, struct newconn *nc);
void cclink_attach_remotecle_node(struct cclink *ccl, struct nodelink *callee_node);

void cclink_list_delete_by_client(struct client *cl);
void cclink_list_delete_by_nodelink(struct nodelink *node);

struct client* client_tree_search(const unsigned char *id);//
struct client *client_list_addnew(struct newconn *nc);
void client_list_delete(struct client* ci);
struct PICA_proto_msg* client_wbuf_push(struct client *c, unsigned int msgid, unsigned int size);
int client_rbuf_grow(struct client *c);

struct PICA_proto_msg* nodelink_wbuf_push(struct nodelink *nl, unsigned int msgid, unsigned int size);
int nodelink_rbuf_grow(struct nodelink *nl);
int nodelink_attach_nodeaddr(struct nodelink *nl, unsigned int addr_type, void* nodeaddr, unsigned int nodeaddr_size);
struct nodelink *nodelink_list_addnew(struct newconn *nc);
void nodelink_list_delete(struct nodelink *n);
struct nodelink *nodelink_search_by_ipv4addr(in_addr_t addr, in_port_t port); /* arguments are in network byte order */

void newconn_close(struct newconn* nc);
void newconn_free(struct newconn* nc);
struct newconn* newconn_add(struct newconn *ncs, int *pos);
void newconns_init();

void PICA_node_joinskynet(const char* addrlistfilename, const char *my_addr);

#define MSGINFO_MSGSNUM(arr) (sizeof(arr)/sizeof(struct PICA_msginfo))

struct PICA_msginfo  _msginfo_comm[] =
{
	{PICA_PROTO_CONNREQOUTG, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNREQOUTG_SIZE, procmsg_CONNREQOUTG},
	{PICA_PROTO_CONNALLOW, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNALLOW_SIZE, procmsg_CONNALLOW},
	{PICA_PROTO_CONNDENY, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNDENY_SIZE, procmsg_CONNDENY},
	{PICA_PROTO_CLNODELISTREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_CLNODELISTREQ_SIZE, procmsg_NODELISTREQ},
	{PICA_PROTO_PINGREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREQ_SIZE, procmsg_PINGREQ_client},
	{PICA_PROTO_PINGREP, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREP_SIZE, procmsg_PINGREP_client},
	{PICA_PROTO_MULTILOGIN, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_MULTILOGIN_client}
};

struct PICA_msginfo _msginfo_node[] =
{
	{PICA_PROTO_INITRESPOK, PICA_MSG_FIXED_SIZE, PICA_PROTO_INITRESPOK_SIZE, procmsg_NODERESP},
	{PICA_PROTO_VERDIFFER, PICA_MSG_FIXED_SIZE, PICA_PROTO_VERDIFFER_SIZE, procmsg_NODERESP},
	{PICA_PROTO_NEWNODE_IPV4, PICA_MSG_FIXED_SIZE, PICA_PROTO_NEWNODE_IPV4_SIZE, procmsg_NEWNODE},
	{PICA_PROTO_NODELISTREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_NODELISTREQ_SIZE, procmsg_NODELISTREQ},
	{PICA_PROTO_NODELIST, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_NODELIST},

	{PICA_PROTO_SEARCH, PICA_MSG_FIXED_SIZE, PICA_PROTO_SEARCH_SIZE, procmsg_SEARCH},
	{PICA_PROTO_N2NFOUND, PICA_MSG_FIXED_SIZE, PICA_PROTO_N2NFOUND_SIZE, procmsg_N2NFOUND},
	{PICA_PROTO_N2NFOUNDCACHE, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_N2NFOUND},
	{PICA_PROTO_N2NCONNREQOUTG, PICA_MSG_FIXED_SIZE, PICA_PROTO_N2NCONNREQOUTG_SIZE, procmsg_N2NCONNREQOUTG},
	{PICA_PROTO_N2NALLOW, PICA_MSG_FIXED_SIZE, PICA_PROTO_N2NALLOW_SIZE, procmsg_N2NALLOW},
	{PICA_PROTO_N2NNOTFOUND, PICA_MSG_FIXED_SIZE, PICA_PROTO_N2NNOTFOUND_SIZE, procmsg_N2NNOTFOUND},
	{PICA_PROTO_N2NMSG, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_N2NMSG},
	{PICA_PROTO_PINGREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREQ_SIZE, procmsg_PINGREQ_node},
	{PICA_PROTO_PINGREP, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREP_SIZE, procmsg_PINGREP_node},
	{PICA_PROTO_MULTILOGIN, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_MULTILOGIN_node}
};

struct PICA_msginfo _msginfo_newconn[] =
{
	{PICA_PROTO_INITREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_INITREQ_SIZE, procmsg_INITREQ},
	{PICA_PROTO_CONNID, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNID_SIZE, procmsg_CONNID},
	{PICA_PROTO_NODECONNREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_NODECONNREQ_SIZE, procmsg_NODECONNREQ}
};

struct PICA_msginfo  _msginfo_multi[] =
{
	{PICA_PROTO_CONNREQOUTG, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNREQOUTG_SIZE, procmsg_dummy_multi_c2n},
	{PICA_PROTO_CONNALLOW, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNALLOW_SIZE, procmsg_dummy_multi_c2n},
	{PICA_PROTO_CONNDENY, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNDENY_SIZE, procmsg_dummy_multi_c2n},
	{PICA_PROTO_CLNODELISTREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_CLNODELISTREQ_SIZE, procmsg_NODELISTREQ},
	{PICA_PROTO_PINGREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREQ_SIZE, procmsg_PINGREQ_client},
	{PICA_PROTO_PINGREP, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREP_SIZE, procmsg_PINGREP_client},
	{PICA_PROTO_MULTILOGIN, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_MULTILOGIN_client}
};

static int process_async_ssl_readwrite(SSL *ssl, int ret)
{
	if (ret == 0)
		return 0;

	if (ret < 0)
	{
		switch(SSL_get_error(ssl, ret))
		{
		case SSL_ERROR_WANT_WRITE:
		case SSL_ERROR_WANT_READ:
			break;
		default:
			return 0;
		}
	}

	return 1;
}

static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
	//return 1 for self-signed certificates
	char    buf[256];
	X509   *err_cert;
	int     err, depth;

	err_cert = X509_STORE_CTX_get_current_cert(ctx);
	err = X509_STORE_CTX_get_error(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);

	X509_NAME_oneline(X509_get_subject_name(err_cert), buf, 256);

	if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT && depth == 0)
		preverify_ok = 1;

	if (!preverify_ok)
	{
		PICA_info("certificate verification error:num=%d:%s:depth=%d:%s\n", err, X509_verify_cert_error_string(err), depth, buf);
	}

	return preverify_ok;
}

unsigned int procmsg_INITREQ(unsigned char* buf, unsigned int size, void* ptr)
{
	struct newconn *nc = (struct newconn*)ptr;
	struct PICA_proto_msg *mp;

	PICA_debug1("received INITREQ");

	nc->type = NEWCONN_C2N;

	if (buf[2] == PICA_PROTO_VER_HIGH && buf[3] == PICA_PROTO_VER_LOW)
	{
		struct client *c;
		c = client_list_addnew(nc);
		if (!c)
			return 0;

		c->state = PICA_CLSTATE_WAITANONTLSSHUTDOWN;
		nc->iconn.cl = c;
		nc->state = PICA_NEWCONN_SENDINGOK;
		/*
		newconn_free(nc);

		if ((mp = client_wbuf_push(c, PICA_PROTO_INITRESPOK, PICA_PROTO_INITRESPOK_SIZE)))
		{
			mp->tail[0] = 'O';
			mp->tail[1] = 'K';
		}

		c->state = PICA_CLSTATE_SENDINGRESP;*/
	}///////---------<<<<<<<<<<< new client
	else
	{
		nc->state = PICA_NEWCONN_SENDINGREJECT;

		if (buf[2] > PICA_PROTO_VER_HIGH || (buf[2] == PICA_PROTO_VER_HIGH && buf[3] > PICA_PROTO_VER_LOW))
		{
			PICA_warn("Client has newer protocol version than node. Please check if update for pica-node is available");
		}
		else
		{
			PICA_info("Client has older protocol version. Disconnecting ...");
		}
	}

	return 1;
}

void cclink_sendcleconnected(struct cclink *link)
{
	struct PICA_proto_msg *mp;

	if (link->state == PICA_CCLINK_LOCAL_WAITCONNCLEANONTLSSHUTDOWN)
	{
		if((mp = client_wbuf_push(link->p1, PICA_PROTO_FOUND, PICA_PROTO_FOUND_SIZE)))
		{
			memcpy(mp->tail, link->callee_id, PICA_ID_SIZE);
		}
		else
		{
			cclink_list_delete(link);
			return;
		}
	}

	if (link->state == PICA_CCLINK_N2NCLE_WAITCONNCLEANONTLSSHUTDOWN)
	{
		if((mp = nodelink_wbuf_push(link->caller_node, PICA_PROTO_N2NALLOW, PICA_PROTO_N2NALLOW_SIZE)))
		{
			memcpy(mp->tail, link->caller_id, PICA_ID_SIZE);
			memcpy(mp->tail + PICA_ID_SIZE, link->callee_id, PICA_ID_SIZE);
		}
		else
		{
			cclink_list_delete(link);
			return;
		}
	}

}

unsigned int procmsg_CONNID(unsigned char* buf, unsigned int size, void* ptr)
{
	struct PICA_proto_msg *mp;
	struct newconn *nc = (struct newconn*)ptr;
	unsigned char *caller_id, *callee_id;
	struct cclink *link;

	PICA_debug1("received CONNID");

	nc->type = NEWCONN_C2C;

	caller_id = buf + 2;
	callee_id = buf + 2 + PICA_ID_SIZE;

	{
		char caller_id_buf[2 * PICA_ID_SIZE], callee_id_buf[2 * PICA_ID_SIZE];
		PICA_debug1("CONNID: caller_id=%s callee_id=%s", PICA_id_to_base64(caller_id, caller_id_buf), PICA_id_to_base64(callee_id, callee_id_buf));
	}

	link = cclink_list_search(caller_id, callee_id);

	if (!link)
	{
		PICA_debug3("corresponding cclink not found");
		return 0;
	}

	if (link->state != PICA_CCLINK_LOCAL_WAITCONNCLE && link->state != PICA_CCLINK_LOCAL_WAITCONNCLR
	        && link->state != PICA_CCLINK_N2NCLR_WAITCONNCLR  && link->state != PICA_CCLINK_N2NCLE_WAITCONNCLE)
	{
		PICA_debug2("found cclink is not in expected state");
		return 0;
	}

	{
		struct client *c;
//проверка IP адреса.
//IP адрес должен совпадать с адресом  клиента в управляющем соединении

		if (link->state == PICA_CCLINK_LOCAL_WAITCONNCLE || link->state == PICA_CCLINK_N2NCLE_WAITCONNCLE)
			c = link->p2;
		else if (link->state == PICA_CCLINK_LOCAL_WAITCONNCLR || link->state == PICA_CCLINK_N2NCLR_WAITCONNCLR)
			c = link->p1;

		if (nc->addr.sin_addr.s_addr != c->addr.sin_addr.s_addr)
		{
			PICA_warn("CONNID: IP check failed. IP address of sender does not match IP in c2n connection");
			return 0;
		}
	}

	PICA_debug3("CONNID processed successfully");
	cclink_setwaitanontlsshutdown(link, nc);

	return 1;
}

unsigned int procmsg_NODECONNREQ(unsigned char* buf, unsigned int size, void* ptr)
{
	struct newconn *nc = (struct newconn*)ptr;
	struct PICA_proto_msg *mp;

	PICA_debug1("received NODECONNREQ");

	nc->type = NEWCONN_N2N;

	if (buf[2] == PICA_PROTO_VER_HIGH &&
	        buf[3] == PICA_PROTO_VER_LOW)
	{
		////-прием входящего подключения
		//--------------------------
		nc->state = PICA_NEWCONN_SENDINGOK;
		/*struct nodelink *nl;
		nl = nodelink_list_addnew(nc);
		newconn_free(nc);
		if (!nl)
			return 0;

		if ((mp = nodelink_wbuf_push(nl, PICA_PROTO_INITRESPOK, PICA_PROTO_INITRESPOK_SIZE)))
		{
			mp->tail[0] = 'O';
			mp->tail[1] = 'K';
		}*/
	}
	else
	{
		nc->state = PICA_NEWCONN_SENDINGREJECT;
		/*
		//// PICA_PROTO_VERDIFFER
		send(nc->sck, msg_VERDIFFER, msg_VERDIFFER_len, 0);

		if (buf[2] > PICA_PROTO_VER_HIGH || (buf[2] == PICA_PROTO_VER_HIGH && buf[3] > PICA_PROTO_VER_LOW))
		{
			PICA_warn("Node %s has newer protocol version [%u, %u]. Please check if update for pica-node is available",
			          inet_ntoa(nc->addr.sin_addr), buf[2], buf[3]);
		}
		else
		{
			PICA_info("Node %s has older protocol version. Disconnecting ...", inet_ntoa(nc->addr.sin_addr));
		}

		return 0;*/
	}

	return 1;
}


void mapfunc_sendmultilogin(struct nodelink *p, const unsigned char *msgbuf)
{
	struct PICA_proto_msg *mp;
	uint16_t payload_size = *(uint16_t*)(msgbuf + 2);

	mp = nodelink_wbuf_push(p, PICA_PROTO_MULTILOGIN, payload_size + 4);
	if (mp)
		memcpy(mp->tail, msgbuf + 2, payload_size + 2);
}

void mapfunc_sendsearchreq(struct nodelink *p, const unsigned char *id)
{
	struct PICA_proto_msg *mp;

	mp = nodelink_wbuf_push(p, PICA_PROTO_SEARCH, PICA_PROTO_SEARCH_SIZE);

	if (mp)
		memcpy(mp->tail, id, PICA_ID_SIZE);
}

int nodelink_send_searchreq(const unsigned char *id)
{
	if (!nodelink_list_count)
		return 0;

	LISTMAP(nodelink, nodelink, mapfunc_sendsearchreq, id);

	return 1;
}

unsigned int procmsg_CONNREQOUTG(unsigned char* buf, unsigned int size, void* ptr)
{
	struct client *i, *o; //i - указатель на вызывающего клиента, o - вызываемого(если найден)
	unsigned char *callee_id;
	struct PICA_proto_msg *mp;

	i = (struct client *)ptr;

	PICA_debug1("received CONNREQOUTG from %s", PICA_id_to_base64(i->id, NULL));

	callee_id = buf + 2;

	if ((o = client_tree_search(callee_id)))
	{
		if (cclink_list_search(i->id, callee_id))
		{
			PICA_info("duplicate request for existing c2c connection is ignored");
			return 1;
		}

		if (memcmp(i->id, callee_id, PICA_ID_SIZE) == 0)
		{
			PICA_info("request to create c2c connection to self is ignored");
			return 1;
		}

		//отправить o _CONNREQINC
		if ((mp = client_wbuf_push(o, PICA_PROTO_CONNREQINC, PICA_PROTO_CONNREQINC_SIZE)))
		{
			memcpy(mp->tail, i->id, PICA_ID_SIZE);
			cclink_list_addlocal(i, o);

			o->tmst = time(0);
			o->disconnect_ticking = 1;//if client does not reply in given time period, client is disconnected
			return 1;
		}

		return 1;//сообщение отправить не удалось, но все равно возвращаем 1
	}

	if (cclink_list_findwaitsearch(callee_id)/*search request for this id is already in progress*/
	        || nodelink_send_searchreq(callee_id) /*make new search*/)
	{
		cclink_list_addn2nclr(i, callee_id);
		return 1;
	}

//отправить i _NOTFOUND
	if ((mp = client_wbuf_push(i, PICA_PROTO_NOTFOUND, PICA_PROTO_NOTFOUND_SIZE)))
	{
		memcpy(mp->tail, callee_id, PICA_ID_SIZE);
	}

	return 1;
}

unsigned int procmsg_CONNALLOW(unsigned char* buf, unsigned int size, void* ptr)
{
	struct client *callee;
	struct cclink *link;
	unsigned char *caller_id;

	PICA_debug1("received CONALLOW");

	callee = (struct client *)ptr;
	caller_id = buf + 2;

	callee->disconnect_ticking = 0;
	callee->tmst = time(0);

	link = cclink_list_search(caller_id, callee->id);

	if (!link)
		return 0;

	if (link->state != PICA_CCLINK_LOCAL_WAITREP &&
	        link->state != PICA_CCLINK_N2NCLE_WAITREP)
		return 0;

	cclink_setwaitconn(link);

	return 1;
}

unsigned int procmsg_CONNDENY(unsigned char* buf, unsigned int size, void* ptr)
{
	struct client *caller, *callee;
	struct cclink *link;
	unsigned char *caller_id;
	struct PICA_proto_msg *mp;

	PICA_debug1("received CONNDENY");

	callee = (struct client *)ptr;
	caller_id = buf + 2;

	callee->disconnect_ticking = 0;
	callee->tmst = time(0);

	link = cclink_list_search(caller_id, callee->id);

	if (!link)
		return 0;

	caller = link -> p1;//check for NULL if not LOCAL

	if (link->state != PICA_CCLINK_LOCAL_WAITREP &&
	        link->state != PICA_CCLINK_N2NCLE_WAITREP)
	{
		cclink_list_delete(link);
		return 0;
	}

	if (link->state == PICA_CCLINK_LOCAL_WAITREP)
	{
		//отправить caller _NOTFOUND
		if ((mp = client_wbuf_push(caller, PICA_PROTO_NOTFOUND, PICA_PROTO_NOTFOUND_SIZE)))
		{
			memcpy(mp->tail, callee->id, PICA_ID_SIZE);
		}
	}
	else if (link->state == PICA_CCLINK_N2NCLE_WAITREP)
	{
		//отправить caller N2NNOTFOUND
		if ((mp = nodelink_wbuf_push(link->caller_node, PICA_PROTO_N2NNOTFOUND, PICA_PROTO_N2NNOTFOUND_SIZE)))
		{
			memcpy(mp->tail, caller_id, PICA_ID_SIZE);
			memcpy(mp->tail + PICA_ID_SIZE, callee->id, PICA_ID_SIZE);
		}
	}

	cclink_list_delete(link);

	return 1;
}

unsigned int procmsg_PINGREQ_client(unsigned char* buf, unsigned int size, void* ptr)
{
	struct client *c = (struct client*)ptr;
	struct PICA_proto_msg *mp;

	PICA_debug3("PINGREQ");

	if ((mp = client_wbuf_push(c, PICA_PROTO_PINGREP, PICA_PROTO_PINGREP_SIZE)))
	{
		RAND_pseudo_bytes(mp->tail, 2);
	}
	else
		return 0;

	return 1;
}

unsigned int procmsg_PINGREP_client(unsigned char* buf, unsigned int size, void* ptr)
{
	struct client *c = (struct client*)ptr;

	PICA_debug3("PINGREP c2n");

	c->disconnect_ticking = 0;
	c->tmst = time(0);

	return 1;
}

unsigned int procmsg_MULTILOGIN_client(unsigned char* buf, unsigned int size, void* ptr)
{
	struct client *other, *me = (struct client*)ptr;

	PICA_debug3("MULTILOGIN c2n");

	//send to other instances connected to this node
	other = client_tree_search(me->id);

	while(other)
	{
		if (other != me)
		{
			struct PICA_proto_msg *mp;

			if ((mp = client_wbuf_push(other, PICA_PROTO_MULTILOGIN, size)))
			{
				memcpy(mp->tail, buf + 2, size - 2);
			}
		}

		other = other->next_multi;
	}
	//relay to other nodes
	LISTMAP(nodelink, nodelink, mapfunc_sendmultilogin, buf);

	return 1;
}

unsigned int procmsg_MULTILOGIN_node(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	struct client *c = client_list_head;
	uint64_t timestamp;
	void *sigdatas[4];
	int sigdatalengths[4];
	uint16_t payload_len = *(uint16_t*)(buf + 2);
	int ret;
	int siglen;
	unsigned char *sig;

	PICA_debug3("MULTILOGIN n2n");

	if (payload_len <= sizeof timestamp + PICA_PROTO_NODELIST_ITEM_IPV4_SIZE)
		return 0;

	timestamp = *(uint64_t*)(buf + 4);
	//TODO compare timestamps

	sigdatalengths[0] = PICA_ID_SIZE;

	//add timestamp to signature data
	sigdatas[1] = buf + 4;
	sigdatalengths[1] = sizeof(uint64_t);

	//add sender node address to signature data
	sigdatas[2] = buf + 4 + sizeof(uint64_t);

	switch (buf[4 + sizeof(uint64_t)])
	{
	case PICA_PROTO_NEWNODE_IPV4:
		sigdatalengths[2] = PICA_PROTO_NODELIST_ITEM_IPV4_SIZE;
		break;

	case PICA_PROTO_NEWNODE_IPV6:
		sigdatalengths[2] = PICA_PROTO_NODELIST_ITEM_IPV6_SIZE;
		break;

	case PICA_PROTO_NEWNODE_DNS:
		sigdatalengths[2] = 4 + buf[5 + sizeof(uint64_t)];
	default:
		PICA_warn("Received MULTILOGIN message with invalid node address");
		return 1;
	}

	siglen = payload_len - sizeof(uint64_t) - sigdatalengths[2];
	sig = buf + 4 + sizeof(uint64_t) + sigdatalengths[2];

	sigdatas[3] = NULL;
	sigdatalengths[3] = 0;

	while(c)
	{
		//check signature against current client ID and data in MULTILOGIN packet
		if (c->state == PICA_CLSTATE_CONNECTED)
		{
			EVP_PKEY *pubkey;
			X509* x;

			sigdatas[0] = c->id;

			if ((x = SSL_get_peer_certificate(c->ssl_comm)) &&
				(pubkey = X509_get_pubkey(x)))
			{
				ret = PICA_signverify(pubkey, sigdatas, sigdatalengths, sig, siglen);

				EVP_PKEY_free(pubkey);
				X509_free(x);

				if (ret == 1)
				{
					PICA_debug3("found matching client connection for incoming n2n MULTILOGIN message");
					break;
				}
			}
		}

		c = c->next;
	}

	if (!c)
	{
		PICA_debug3("no matching client found for receved n2n MULTILOGIN");
		return 1;
	}

	while(c) //relay received MULTILOGIN to each client with same id
	{
		struct PICA_proto_msg *mp;

		mp = client_wbuf_push(c, PICA_PROTO_MULTILOGIN, size);

		if (mp)
		{
			memcpy(mp, buf, size);
		}

		c = c->next_multi;
	}

	return 1;
}

unsigned int procmsg_dummy_multi_c2n(unsigned char* buf, unsigned int size, void* ptr)
{
	PICA_debug3("message %X from %p is handled by dummy procmsg", buf[0], ptr);

	return 1;
}

unsigned int procmsg_PINGREQ_node(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	struct PICA_proto_msg *mp;

	PICA_debug3("PINGREQ");

	if ((mp = nodelink_wbuf_push(n, PICA_PROTO_PINGREP, PICA_PROTO_PINGREP_SIZE)))
	{
		RAND_pseudo_bytes(mp->tail, 2);
	}
	else
		return 0;

	return 1;
}

unsigned int procmsg_PINGREP_node(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;

	PICA_debug3("PINGREP n2n");

	n->disconnect_ticking = 0;
	n->tmst = time(0);

	return 1;
}

unsigned int procmsg_NODERESP(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	struct PICA_proto_msg *mp;

	PICA_debug1("received NODERESP");

	switch(buf[0])
	{
	case PICA_PROTO_INITRESPOK:
		if (buf[2] != 'O' || buf[3] != 'K')
			return 0;


		mp = nodelink_wbuf_push(n, PICA_PROTO_NEWNODE_IPV4, PICA_PROTO_NEWNODE_IPV4_SIZE);//IPv6
		if (mp)
		{
			*((in_addr_t*)(mp->tail)) = inet_addr(nodecfg.announced_addr);
			*((in_port_t*)(mp->tail + 4)) = (nodecfg.listen_port ? htons(atoi(nodecfg.listen_port)) :  htons(PICA_COMM_PORT)); //CONF;
		}
		else
			return 0;

		mp = nodelink_wbuf_push(n, PICA_PROTO_NODELISTREQ, PICA_PROTO_NODELISTREQ_SIZE);
		if (mp)
		{
			RAND_pseudo_bytes(mp->tail, 2);
		}
		else
			return 0;

		break;
	case PICA_PROTO_VERDIFFER:
		if (buf[2] > PICA_PROTO_VER_HIGH ||  (buf[3] > PICA_PROTO_VER_LOW && buf[2] == PICA_PROTO_VER_HIGH))
		{
			PICA_warn("make update of node software - peer node has newer protocol version");
		}
		else
		{
			//peer node has older software version
			PICA_info("peer node has older protocol version");
		}
		return 0;
		break;
	}

	return 1;
}

unsigned int procmsg_NEWNODE(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	struct PICA_nodeaddr na;

	PICA_debug1("received NEWNODE");

	switch (buf[0])
	{
	case PICA_PROTO_NEWNODE_IPV4:
		PICA_debug1("NEWNODE_IPV4: ip %.16s port %hu\n", inet_ntoa(*(struct in_addr*)(buf + 2)), ntohs(*(in_port_t*)(buf + 6)));
		{
			struct PICA_nodeaddr_ipv4 na_ipv4;

			na_ipv4.magick = PICA_PROTO_NEWNODE_IPV4;
			na_ipv4.addr = *(in_addr_t*)(buf + 2);
			na_ipv4.port = *(in_port_t*)(buf + 6);

			if (nodelink_search_by_ipv4addr(na_ipv4.addr, na_ipv4.port)) //connection with this node is already established
			{
				PICA_debug1("disconnecting node %.16s:%hu - connection with this node is already established", inet_ntoa(*(struct in_addr*)&na_ipv4.addr), ntohs(na_ipv4.port));
				return 0;
			}

			if (nodecfg.disable_reserved_addrs && PICA_is_reserved_addr_ipv4(na_ipv4.addr))
			{
				PICA_debug1("disconnecting node %.16s:%hu - private IP ranges are rejected by node configuration",
				            inet_ntoa(*(struct in_addr*)&na_ipv4.addr), ntohs(na_ipv4.port)
				           );
				return 0;
			}

			sprintf(na.addr, "%.16s", inet_ntoa(*(struct in_addr*)&na_ipv4.addr));
			na.port = ntohs(na_ipv4.port);

			//int nodelink_attach_nodeaddr(struct nodelink *nl, unsigned int addr_type, void* nodeaddr, unsigned int nodeaddr_size)
			nodelink_attach_nodeaddr(n, na_ipv4.magick, &na_ipv4, sizeof(struct PICA_nodeaddr_ipv4));
		}
		break;
	case PICA_PROTO_NEWNODE_IPV6:
#warning "TO DO"
		break;
	case PICA_PROTO_NEWNODE_DNS:
#warning "TODO"
		break;
	}

	PICA_nodeaddr_save(nodecfg.nodes_db_file, &na);//CONF filename

	return 1;
}

unsigned int procmsg_NODELISTREQ(unsigned char* buf, unsigned int size, void* ptr)
{
	struct PICA_proto_msg *mp;
	struct nodelink *nlp;
	unsigned int nl_size = 0;
	struct nodelink *n = 0;
	struct client *c = 0;
	unsigned char *nl_buf = 0;

	PICA_debug1("received NODELISTREQ");

	switch(buf[0])
	{
	case PICA_PROTO_CLNODELISTREQ:
		c = (struct client *)ptr;
		break;
	case PICA_PROTO_NODELISTREQ:
		n = (struct nodelink *)ptr;
		break;
	}

	nl_buf = (unsigned char*)malloc(65536);

	if (!nl_buf)
		return 0;//ERR


	{
		//TEMP FIXME CONF
//собственный адрес узла
		nl_buf[nl_size] = PICA_PROTO_NEWNODE_IPV4;
		*((in_addr_t*)(nl_buf + nl_size + 1)) = inet_addr(my_addr);
		*((in_port_t*)(nl_buf + nl_size + 5)) = (nodecfg.listen_port ? htons(atoi(nodecfg.listen_port)) :  htons(PICA_COMM_PORT)); //CONF
		nl_size += PICA_PROTO_NODELIST_ITEM_IPV4_SIZE;
	}

	nlp = nodelink_list_head;
	while(nlp && nl_size < 65532)//MAP
	{
		switch(nlp->addr_type)
		{
		case PICA_PROTO_NEWNODE_IPV4:
			if ( PICA_PROTO_NODELISTREQ == buf[0] &&
			        ((struct PICA_nodeaddr_ipv4*)nlp->node_addr)->addr == ((struct PICA_nodeaddr_ipv4*)n->node_addr)->addr    &&
			        ((struct PICA_nodeaddr_ipv4*)nlp->node_addr)->port == ((struct PICA_nodeaddr_ipv4*)n->node_addr)->port )
				break;//exclude requester's address

			nl_buf[nl_size] = PICA_PROTO_NEWNODE_IPV4;
			*((in_addr_t*)(nl_buf + nl_size + 1)) = ((struct PICA_nodeaddr_ipv4*)nlp->node_addr)->addr;
			*((in_port_t*)(nl_buf + nl_size + 5)) = ((struct PICA_nodeaddr_ipv4*)nlp->node_addr)->port;
			nl_size += PICA_PROTO_NODELIST_ITEM_IPV4_SIZE;//CONF
			break;
		}
//#warning "set limit on list size, <65532 bytes!!!"
		nlp = nlp->next;
	}

	switch(buf[0])
	{
	case PICA_PROTO_CLNODELISTREQ:
		mp =  client_wbuf_push( c, PICA_PROTO_CLNODELIST, nl_size + 4);
		break;
	case PICA_PROTO_NODELISTREQ:
		mp = nodelink_wbuf_push( n, PICA_PROTO_NODELIST, nl_size + 4);
		break;
	}

	if (mp)
	{
		*((uint16_t*)mp->tail) = nl_size;
		memcpy(mp->tail + 2, nl_buf, nl_size);
	}

	free(nl_buf);
	return 1;
}

unsigned int procmsg_NODELIST(unsigned char* buf, unsigned int size, void* ptr)
{
	struct PICA_nodeaddr_ipv4 na_ipv4;

	struct nodelink *n = (struct nodelink *)ptr;
	unsigned int listsize = size - 2 - PICA_MSG_VARSIZE_INT16;
	unsigned int listleft = listsize;

	PICA_debug1("received NODELIST");

	while(listleft)
	{
#warning "strict error checking!!! -- listleft overflow, unknown list items, check listleft >= possible item size"
		if (listleft == listsize) //first entry, sender node's address
		{
			switch(buf[size - listleft])
			{
			case PICA_PROTO_NEWNODE_IPV4:
				na_ipv4.magick = PICA_PROTO_NEWNODE_IPV4;
				na_ipv4.addr = *(in_addr_t*)(buf + size - listleft + 1);
				na_ipv4.port = *(in_port_t*)(buf + size - listleft + 5);
				nodelink_attach_nodeaddr(n, na_ipv4.magick, &na_ipv4, sizeof(struct PICA_nodeaddr_ipv4));

				listleft -= PICA_PROTO_NODELIST_ITEM_IPV4_SIZE;
				break;
			}
			continue;
		}
		else
		{
			struct PICA_nodeaddr na;
			//char addr[256];
			struct newconn nc;
			struct nodelink *nlp;
			int skip_save = 0;

			memset(na.addr, 0, 256);
			memset(&nc, 0, sizeof(struct newconn));

			switch(buf[size - listleft])
			{
			case PICA_PROTO_NEWNODE_IPV4:
				na_ipv4.magick = PICA_PROTO_NEWNODE_IPV4;
				na_ipv4.addr = *(in_addr_t*)(buf + size - listleft + 1);
				na_ipv4.port = *(in_port_t*)(buf + size - listleft + 5);

				sprintf(na.addr, "%.16s", inet_ntoa(*(struct in_addr*)&na_ipv4.addr));
				na.port = ntohs(na_ipv4.port);

				//nodewait_start_connection(&na);
// 			if (try_connect_to_node(na.addr,ntohs(na_ipv4.port),&nc))
// 				if (try_get_reply(&nc))
// 					{
// 					nlp=nodelink_list_addnew(&nc);
// 					send_newnode(nlp,my_addr);
// 					nodelink_attach_nodeaddr(nlp,na_ipv4.magick,&na_ipv4,sizeof(struct PICA_nodeaddr_ipv4));
// 					PICA_debug1("LINKED NODE:%s\n",inet_ntoa(*(struct in_addr*)&na_ipv4.addr));
// 					}

				PICA_debug3("NODELIST item %.16s:%hu", inet_ntoa(*(struct in_addr*)&na_ipv4.addr), ntohs(na_ipv4.port));

				if (nodecfg.disable_reserved_addrs && PICA_is_reserved_addr_ipv4(na_ipv4.addr))
				{
					skip_save = 1;
					PICA_debug1("Received NODELIST item %.16s:%hu is in private range, skipping",
					            inet_ntoa(*(struct in_addr*)&na_ipv4.addr), ntohs(na_ipv4.port)
					           );
				}
				listleft -= PICA_PROTO_NODELIST_ITEM_IPV4_SIZE;
				break;
			}
			if (!skip_save)
				PICA_nodeaddr_save(nodecfg.nodes_db_file, &na);//CONF filename
		}
	}

	return 1;
}

unsigned int procmsg_N2NFOUND(unsigned char* buf, unsigned int size, void* ptr)
{
	struct cclink *cc;
	struct nodelink *n = (struct nodelink *)ptr;
	unsigned char *callee_id = buf + 2;
	struct PICA_proto_msg *mp;

	PICA_debug1("received N2NFOUND");

	while((cc = cclink_list_findwaitsearch(callee_id)))
	{
		cclink_attach_remotecle_node(cc, n);
		if((mp = nodelink_wbuf_push(n, PICA_PROTO_N2NCONNREQOUTG, PICA_PROTO_N2NCONNREQOUTG_SIZE)))
		{
			memcpy(mp->tail, cc->caller_id, PICA_ID_SIZE);
			memcpy(mp->tail + PICA_ID_SIZE, callee_id, PICA_ID_SIZE);

		}
	}
	return 1;
}

unsigned int procmsg_SEARCH(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	unsigned char *callee_id = buf + 2;
	struct PICA_proto_msg *mp;
	struct client *callee;

	PICA_debug1("received SEARCH");

	if ((callee = client_tree_search(callee_id)))
	{
		if ((mp = nodelink_wbuf_push(n, PICA_PROTO_N2NFOUND, PICA_PROTO_N2NFOUND_SIZE)))
		{
			memcpy(mp->tail, callee_id, PICA_ID_SIZE);
		}
	}
	return 1;
}

unsigned int procmsg_N2NCONNREQOUTG(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	unsigned char *caller_id, *callee_id;
	struct PICA_proto_msg *mp;
	struct client *callee;

	PICA_debug1("received N2NCONNREQOUTG");

	caller_id = buf + 2;
	callee_id = buf + 2 + PICA_ID_SIZE;

	if ((callee = client_tree_search(callee_id)))
	{
		if ((mp = client_wbuf_push(callee, PICA_PROTO_CONNREQINC, PICA_PROTO_CONNREQINC_SIZE)))
		{
			memcpy(mp->tail, caller_id, PICA_ID_SIZE);
			cclink_list_addn2ncle(caller_id, n, callee);
		}
	}
	else
	{
		if ((mp = nodelink_wbuf_push(n, PICA_PROTO_N2NNOTFOUND, PICA_PROTO_N2NNOTFOUND_SIZE)))
		{
			memcpy(mp->tail, caller_id, PICA_ID_SIZE);
			memcpy(mp->tail + PICA_ID_SIZE, callee_id, PICA_ID_SIZE);
		}
	}
	return 1;
}

unsigned int procmsg_N2NALLOW(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	unsigned char *caller_id, *callee_id;
	struct PICA_proto_msg *mp;
	struct cclink *cc;

	PICA_debug1("received N2NALLOW");

	caller_id = buf + 2;
	callee_id = buf + 2 + PICA_ID_SIZE;

	cc = cclink_list_search(caller_id, callee_id);

	if (cc)
	{
		if ((mp = client_wbuf_push(cc->p1, PICA_PROTO_FOUND, PICA_PROTO_FOUND_SIZE)))
		{
			memcpy(mp->tail, callee_id, PICA_ID_SIZE);
			cclink_setwaitconn(cc);
		}
	}
	return 1;
}

unsigned int procmsg_N2NNOTFOUND(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	unsigned char *caller_id, *callee_id;
	struct PICA_proto_msg *mp;
	struct cclink *cc;

	PICA_debug1("received N2NNOTFOUND");

	caller_id = buf + 2;
	callee_id = buf + 2 + PICA_ID_SIZE;

	cc = cclink_list_search(caller_id, callee_id);

	if (cc)
	{
		if ((mp = client_wbuf_push(cc->p1, PICA_PROTO_NOTFOUND, PICA_PROTO_NOTFOUND_SIZE)))
		{
			memcpy(mp->tail, callee_id, PICA_ID_SIZE);
		}
		cclink_list_delete(cc);
	}

	return 1;
}

unsigned int procmsg_N2NMSG(unsigned char* buf, unsigned int size, void* ptr)
{
	struct nodelink *n = (struct nodelink *)ptr;
	struct PICA_proto_msg *mp;
	unsigned char *sender_id, *receiver_id;
	struct cclink *cc;
	unsigned int len, datalen;

	PICA_debug1("received N2NMSG");

	len = *(unsigned short*)(buf + 2);
	sender_id = buf + 4;
	receiver_id = buf + 4 + PICA_ID_SIZE;

	if (len < 2 * PICA_ID_SIZE)
		return 0;

	datalen = len - 2 * PICA_ID_SIZE;

	cc = cclink_list_search(sender_id, receiver_id);

	if (cc && cc->state == PICA_CCLINK_N2NCLE_ACTIVE)
	{
		if (datalen + cc->bufpos_p1p2 > cc->buflen_p1p2)
		{
			cclink_list_delete(cc);//CLOSING CCLINK DUE TO BUFER OVERRUN --------- FIXME FIX FIX FIX
			return 1;
		}

		memcpy(cc->buf_p1p2 + cc->bufpos_p1p2, buf + 4 + 2 * PICA_ID_SIZE, datalen);
		cc->bufpos_p1p2 += datalen;
		return 1;
	}
///////////////
	cc = cclink_list_search(receiver_id, sender_id);

	if (cc && cc->state == PICA_CCLINK_N2NCLR_ACTIVE)
	{
		if (datalen + cc->bufpos_p2p1 > cc->buflen_p2p1)
		{
			cclink_list_delete(cc);//CLOSING CCLINK DUE TO BUFER OVERRUN --------- FIXME FIX FIX FIX
			return 1;
		}

		memcpy(cc->buf_p2p1 + cc->bufpos_p2p1, buf + 4 + 2 * PICA_ID_SIZE, datalen);
		cc->bufpos_p2p1 += datalen;
		return 1;
	}

	if ((mp = nodelink_wbuf_push(n, PICA_PROTO_N2NNOTFOUND, PICA_PROTO_N2NNOTFOUND_SIZE)))
	{
		memcpy(mp->tail, sender_id, PICA_ID_SIZE);
		memcpy(mp->tail + PICA_ID_SIZE, receiver_id, PICA_ID_SIZE);
	}

	return 1;
}


struct cclink* cclink_list_findwaitsearch(const unsigned char *callee_id)
{
	struct cclink *c;
	c = cclink_list_head;
	while(c)
	{
		if (memcmp(c->callee_id, callee_id, PICA_ID_SIZE) == 0
		        &&  c->state == PICA_CCLINK_N2NCLR_WAITSEARCH)
			return c;
		c = c->next;
	}
	return 0;
}

struct cclink* cclink_list_search(const unsigned char *caller_id, const unsigned char *callee_id)
{
	struct cclink *c;
	c = cclink_list_head;

	while(c)
	{
		if (memcmp(c->caller_id, caller_id, PICA_ID_SIZE) == 0
		        &&   memcmp(c->callee_id, callee_id, PICA_ID_SIZE) == 0)
			return c;
		c = c->next;
	}
	return 0;
}

void cclink_list_delete(struct cclink *l)
{
	PICA_debug1("cclink_list_delete(%p)", (void*)l);
//удаление из глобального списка
	if (l->prev)
		l->prev->next = l->next;
	else
		cclink_list_head = l->next;

	if (l->next)
		l->next->prev = l->prev;
	else
		cclink_list_end = l->prev;

	if (l->p1)
	{
		if (l->sck_p1)
		{
			SHUTDOWN(l->sck_p1);
			CLOSE(l->sck_p1);
			PICA_debug2("closed c2c socket %i", l->sck_p1);
		}
	}//caller_id is stored in p1 structure
	else
		free(l->caller_id);//caller_id was allocated by malloc

	if (l->p2)
	{
		if (l->sck_p2)
		{
			SHUTDOWN(l->sck_p2);
			CLOSE(l->sck_p2);
			PICA_debug2("closed c2c socket %i", l->sck_p2);
		}
	}
	else
		free(l->callee_id);

	if (l->buf_p1p2)
		free(l->buf_p1p2);

	if (l->buf_p2p1)
		free(l->buf_p2p1);

	free(l);
	cclink_list_count--;
}

void cclink_list_delete_by_client(struct client *cl)
{
	struct cclink *kill_ptr = 0, *l = cclink_list_head;
	PICA_debug1("cclink_list_delete_by_client(%p)", (void*)cl);

	while(l)
	{
		if (l->p1 == cl || l->p2 == cl)
			kill_ptr = l;

		l = l->next;

		if (kill_ptr)
		{
			cclink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}

}

void cclink_list_delete_by_nodelink(struct nodelink *node)
{
	struct cclink *kill_ptr = 0, *l = cclink_list_head;

	PICA_debug1("cclink_list_delete_by_nodelink(%p)", (void*)node);

	while(l)
	{
		if (l->caller_node == node || l->callee_node == node)
			kill_ptr = l;

		l = l->next;

		if (kill_ptr)
		{
			cclink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}


//clr - caller
//cle - callee
// null value of clr or cle means that corresponding client is on remote node, is not local client
struct cclink* cclink_list_add(struct client *clr, struct client *cle)
{
	struct cclink *l;

	l = (struct cclink*)calloc(sizeof(struct cclink), 1);

	if (!l)
		return 0;

//l->state=PICA_LINKSTATE_WAITREP;

	l->p1 = clr;
	l->p2 = cle;

//добавление в глобальный список
	if (cclink_list_head)
	{
		cclink_list_end->next = l;
		l->prev = cclink_list_end;
		cclink_list_end = l;
	}
	else
	{
		cclink_list_head = l;
		cclink_list_end = l;
	}

//timestamp
	l->tmst = time(0);

	cclink_list_count++;

	return l;
}


void cclink_list_addlocal(struct client *clr, struct client *cle)
{
	struct cclink *l;

	l = cclink_list_add(clr, cle);

	if (l)
	{
		l->state = PICA_CCLINK_LOCAL_WAITREP;
		l->caller_id = l->p1->id;
		l->callee_id = l->p2->id;
	}

}

void cclink_list_addn2nclr(struct client *clr, const unsigned char *callee_id)
{
	struct cclink *l;

	l = cclink_list_add(clr, 0);

	if (l)
	{
		l->state = PICA_CCLINK_N2NCLR_WAITSEARCH;
		l->caller_id = l->p1->id;
		l->callee_id = (unsigned char*)malloc(PICA_ID_SIZE);

		if (l->callee_id == NULL)
			PICA_fatal("memory allocation failed");

		memcpy(l->callee_id, callee_id, PICA_ID_SIZE);
	}
}

void cclink_attach_remotecle_node(struct cclink *ccl, struct nodelink *callee_node)
{
	if (ccl && ccl->state == PICA_CCLINK_N2NCLR_WAITSEARCH)
	{
		ccl->state = PICA_CCLINK_N2NCLR_WAITREP;
		ccl->callee_node = callee_node;
	}
}

void cclink_list_addn2ncle(const unsigned char *caller_id, struct nodelink *caller_node, struct client *cle)
{
	struct cclink *l;

	l = cclink_list_add(0, cle);

	if (l)
	{
		l->state = PICA_CCLINK_N2NCLE_WAITREP;
		l->caller_id = (unsigned char*)malloc(PICA_ID_SIZE);

		if (l->caller_id == NULL)
			PICA_fatal("memory allocation failed");

		memcpy(l->caller_id, caller_id, PICA_ID_SIZE);
		l->callee_id = l->p2->id;
		l->caller_node = caller_node;
	}

}

void cclink_setwaitconn(struct cclink *ccl)
{
	switch (ccl->state)
	{
	case PICA_CCLINK_LOCAL_WAITREP:
		ccl->state = PICA_CCLINK_LOCAL_WAITCONNCLE;
		//
		break;
	case PICA_CCLINK_N2NCLR_WAITREP:
		ccl->state = PICA_CCLINK_N2NCLR_WAITCONNCLR;
		break;
	case PICA_CCLINK_N2NCLE_WAITREP:
		ccl->state = PICA_CCLINK_N2NCLE_WAITCONNCLE;
		break;
	default:
		;
	}
}

int cclink_allocbufs(struct cclink *ccl)
{
	ccl->buf_p1p2 = malloc(DEFAULT_BUF_SIZE);
	ccl->buf_p2p1 = malloc(DEFAULT_BUF_SIZE);
	if (!ccl->buf_p1p2 || !ccl->buf_p2p1)
	{
		if (ccl->buf_p1p2)
			free(ccl->buf_p1p2);
		if (ccl->buf_p2p1)
			free(ccl->buf_p2p1);
		return 0;
	}
	ccl->buflen_p1p2 = DEFAULT_BUF_SIZE;
	ccl->buflen_p2p1 = DEFAULT_BUF_SIZE;
	return 1;
}

void cclink_setwaitanontlsshutdown(struct cclink *link, struct newconn *nc)
{
	nc->iconn.cc = link;
	nc->state = PICA_NEWCONN_WAITANONTLSSHUTDOWN;

	switch(link->state)
	{
	case PICA_CCLINK_LOCAL_WAITCONNCLE:
		link->sck_p2 = nc->sck;
		link->state = PICA_CCLINK_LOCAL_WAITCONNCLEANONTLSSHUTDOWN;
		break;

	case PICA_CCLINK_LOCAL_WAITCONNCLR:
		link->sck_p1 = nc->sck;

		if (!cclink_allocbufs(link))
		{
			cclink_list_delete(link);
			return;
		}
		link->state = PICA_CCLINK_LOCAL_WAITCONNCLRANONTLSSHUTDOWN;
		break;

	case PICA_CCLINK_N2NCLR_WAITCONNCLR:
		link->sck_p1 = nc->sck;

		if (!cclink_allocbufs(link))
		{
			cclink_list_delete(link);
			return;
		}
		link->state = PICA_CCLINK_N2NCLR_WAITCONNCLRANONTLSSHUTDOWN;
		break;

	case PICA_CCLINK_N2NCLE_WAITCONNCLE:
		link->sck_p2 = nc->sck;

		if (!cclink_allocbufs(link))
		{
			cclink_list_delete(link);
			return;
		}

		link->state = PICA_CCLINK_N2NCLE_WAITCONNCLEANONTLSSHUTDOWN;
		break;
	}
}

void cclink_activate(struct cclink *ccl)
{
	switch(ccl->state)
	{
	case PICA_CCLINK_LOCAL_WAITCONNCLEANONTLSSHUTDOWN:
		ccl->state = PICA_CCLINK_LOCAL_WAITCONNCLR;
		break;
	case PICA_CCLINK_LOCAL_WAITCONNCLRANONTLSSHUTDOWN:
		ccl->state = PICA_CCLINK_LOCAL_ACTIVE;
		break;
	case PICA_CCLINK_N2NCLR_WAITCONNCLRANONTLSSHUTDOWN:
		ccl->state = PICA_CCLINK_N2NCLR_ACTIVE;
		break;
	case PICA_CCLINK_N2NCLE_WAITCONNCLEANONTLSSHUTDOWN:
		ccl->state = PICA_CCLINK_N2NCLE_ACTIVE;
		break;
	}
}


int PICA_node_init()
{
	struct sockaddr_in sd;
	int ret;
	int flag;
	FILE *dh_file;
	DH *dh = NULL;

	SSL_load_error_strings();
	SSL_library_init();


#ifdef WIN32
	WSADATA wsd;
	WSAStartup(MAKEWORD(2, 2), &wsd);
#endif

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

//CONF -AF_INET6 ????
	listen_comm_sck = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (listen_comm_sck == -1)
		PICA_fatal("unable to get socket - %s", strerror(errno));

	flag = 1; //enable SO_REUSEADDR
	if (0 != setsockopt(listen_comm_sck, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)))
	{
		PICA_error("failed to set SO_REUSEADDR on socket: %s", strerror(errno));
	}

	memset(&sd, 0, sizeof(sd));

	sd.sin_family = AF_INET;
	sd.sin_addr.s_addr = INADDR_ANY; //CONF
	sd.sin_port = (nodecfg.listen_port ? htons(atoi(nodecfg.listen_port)) :  htons(PICA_COMM_PORT));//CONF

	ret = bind(listen_comm_sck, (struct sockaddr*)&sd, sizeof(sd));

	if (ret == -1)
		PICA_fatal("unable to bind socket - %s", strerror(errno));


	ret = listen(listen_comm_sck, 20);
	if (ret == -1)
		PICA_fatal("unable to listen socket - %s", strerror(errno));

	{
		int ret_comm;
		unsigned long arg = 1;
#ifdef WIN32
		ret_comm = ioctlsocket(listen_comm_sck, FIONBIO, &arg);
// ret_data=ioctlsocket(listen_data_sck,FIONBIO,&arg);
// ret_data=ioctlsocket(listen_node_sck,FIONBIO,&arg);
#else
		ret_comm = ioctl(listen_comm_sck, FIONBIO, (int*)&arg);
// ret_data=ioctl(listen_data_sck,FIONBIO,(int*)&arg);
// ret_data=ioctl(listen_node_sck,FIONBIO,(int*)&arg);
#endif
		if (ret_comm == -1)
			PICA_fatal("unable to set non-blocking mode on socket - %s", strerror(errno));
	}

//#ifdef NO_RAND_DEV
//PICA_rand_seed();
//#endif

	ctx = SSL_CTX_new(TLSv1_2_method());
	anon_ctx = SSL_CTX_new(TLSv1_2_method());

	if (!ctx || !anon_ctx)
		PICA_fatal("unable to create SSL_CTX object");

	ret = SSL_CTX_set_cipher_list(ctx, PICA_TLS_CIPHERLIST);

	if (!ret)
		PICA_fatal("failed to set cipher list " PICA_TLS_CIPHERLIST);

	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);

	dh_file = fopen(nodecfg.dh_param_file, "r");

	if (dh_file)
	{
		dh = PEM_read_DHparams(dh_file, NULL, NULL, NULL);
		fclose(dh_file);
	}
	else
		PICA_fatal("Unable to open DH parameters file %", nodecfg.dh_param_file);

	ret = SSL_CTX_set_tmp_dh(anon_ctx, dh);

	if (ret != 1)
		PICA_fatal("Failed to set DH parameters");

	ret = SSL_CTX_set_cipher_list(anon_ctx, PICA_TLS_ANONDHCIPHERLIST);

	if (!ret)
		PICA_fatal("failed to set cipher list " PICA_TLS_ANONDHCIPHERLIST);

	newconns_init();

	return 1;
}


void newconn_close(struct newconn* nc)
{
	CLOSE(nc->sck);
	PICA_debug3("newconn_close(): nc = %p closed socket %i", nc, nc->sck);

	nc->sck = -1;
	nc->pos = 0;

	if (nc->iconn.cl && nc->type == NEWCONN_C2N)
		client_list_delete(nc->iconn.cl);

	if (nc->iconn.cc && nc->type == NEWCONN_C2C)
		cclink_list_delete(nc->iconn.cc);

	nc->iconn.cl = NULL;

	nc->type = NEWCONN_UNKNOWN;
}

void newconns_init()
{
	int i;

	memset(newconns, 0, sizeof(struct newconn) * MAX_NEWCONNS);

	for (i = 0; i < MAX_NEWCONNS; i++)
	{
		newconns[i].sck = -1;
		newconns[i].type = NEWCONN_UNKNOWN;
	}
}

//функция ищет свободный элемент в массиве структур newconn и возвращает на него указатель
//В случае, если свободный элемент не найден, возвращается элемент с наиболее поздней отметкой времени,
//при этом соединение закрывается
struct newconn* newconn_add(struct newconn *ncs, int *pos)
{
	time_t min;
	int min_pos = 0;
	if (*pos < MAX_NEWCONNS && ncs[*pos].sck == -1)
	{
		goto ret;
	}
	else
	{
		*pos = 0;
		min = ncs->tmst;
		while(*pos < MAX_NEWCONNS && ncs[*pos].sck >= 0)
		{
			if (ncs[*pos].tmst < min)
			{
				min = ncs[*pos].tmst;
				min_pos = *pos;
			}
			(*pos)++;
		}

		if ( (*pos) == MAX_NEWCONNS)
		{
			*pos = min_pos;
			SHUTDOWN(ncs[min_pos].sck);
			CLOSE(ncs[min_pos].sck);
			PICA_debug2("closed newconn socket %i", ncs[min_pos].sck);
		}
	}

ret:
	ncs[*pos].sck = -1;
	ncs[*pos].pos = 0;
	ncs[*pos].tmst = time(0);
	return &ncs[(*pos)++];
}

void newconn_free(struct newconn* nc)
{
	nc->sck = -1;
//nc->pos=0; - pos value is being used in processmsgdatastream
}

int client_tree_add(struct client *ci)
{
	struct client *i;
	if (client_tree_root)
	{
		i = client_tree_root;

		do
		{
			int cmp;

			cmp = memcmp(ci->id, i->id, PICA_ID_SIZE);

			if (cmp == 0)
				return 0;

			if (cmp < 0) // ci->id < i->id
			{
				if (i->left)
					i = i->left;
				else
				{
					i->left = ci;
					ci->up = i;
					return 1;
				}
			}
			else
			{
				if (i->right)
					i = i->right;
				else
				{
					i->right = ci;
					ci->up = i;
					return 1;
				}
			}
		}
		while(1);
	}
	else
		client_tree_root = ci;

	return 1;
}

struct client* client_tree_search(const unsigned char *id)
{
	struct client* i_ptr;
	i_ptr = client_tree_root;

//PICA_debug1("client_tree_search: searching for %u...",id);

	while(i_ptr)
	{
		int cmp = memcmp(id, i_ptr->id, PICA_ID_SIZE);

		if (cmp == 0)
			return i_ptr;

		if (cmp < 0) //id < i_ptr->id
			i_ptr = i_ptr->left;
		else
			i_ptr = i_ptr->right;
	}

	PICA_debug1("client_tree_search- id not found");
	return 0;
}

void client_tree_print(struct client *c)
{
	if (!c)
	{
		puts("NULL");
		return;
	}

	printf("%p: %u LEFT: %p, RIGHT: %p\n", (void*)c, c->id, (void*)c->left, (void*)c->right);

	if (c->left)
		client_tree_print(c->left);

	if (c->right)
		client_tree_print(c->right);
}


void client_attach_multi_secondary(struct client *primary, struct client *secondary)
{
	secondary->next_multi = primary->next_multi;
	primary->next_multi = secondary;
}

void client_tree_remove(struct client* ci)
{
	struct client** p_link;//указывает на left или right в родительском узле

	if (!ci->up)
	{
		p_link = &client_tree_root;
	}
	else
	{
		if (ci == ci->up->left)
			p_link = &(ci->up->left);
		else
			p_link = &(ci->up->right);
	}


	if (!ci->left && !ci->right)
	{
		*p_link = 0;
		return;
	}

	if (ci->left && !ci->right)
	{
		ci->left->up = ci->up;
		*p_link = ci->left;
		return;
	}

	if (!ci->left && ci->right)
	{
		ci->right->up = ci->up;
		*p_link = ci->right;
		return;
	}


	{
		struct client *lm;
		lm = ci->right;
		while(lm->left) lm = lm->left; //поиск самого левого узла правого поддерева

		if (lm != ci->right)
		{
			lm->up->left = lm->right;
			lm->right = ci->right;
		}
		lm->left = ci->left;
		lm->up = ci->up;
		*p_link = lm;
	}
}

struct client *client_list_addnew(struct newconn *nc)
{
	struct client* ci;

	ci = (struct client *)calloc(1, sizeof(struct client));

	if (!ci)	return 0;

	ci->ssl_comm = SSL_new (ctx);

	if (!ci->ssl_comm)
	{
		free(ci);
		return 0;
	}

	if (client_list_end)
	{
		client_list_end->next = ci;
		ci->prev = client_list_end;
		client_list_end = ci;
	}
	else
	{
		client_list_head = ci;
		client_list_end = ci;
	}

	ci->sck_comm = nc->sck;
	ci->addr = nc->addr;


	SSL_set_fd(ci->ssl_comm, ci->sck_comm);

	ci->r_buf = calloc(1, DEFAULT_BUF_SIZE);
	if (!ci->r_buf)
	{
		free(ci);
		return 0;
	}
	ci->buflen_r = DEFAULT_BUF_SIZE;

	ci->tmst = time(0);

	return ci;
}

void client_tree_replace_multi(struct client *primary)
{
	struct client** p_link;
	struct client *secondary;

	PICA_debug2("client_tree_replace_multi(%p)", (void*)primary);

	if (!primary->up)
	{
		p_link = &client_tree_root;
	}
	else
	{
		if (primary == primary->up->left)
			p_link = &(primary->up->left);
		else
			p_link = &(primary->up->right);
	}

	*p_link = primary->next_multi;

	secondary = primary->next_multi;

	secondary->state = primary->state;
	secondary->up = primary->up;
	secondary->left = primary->left;
	secondary->right = primary->right;
}

void client_list_delete(struct client* ci)
{
	PICA_debug1("client_list_delete(%p)", (void*)ci);
	SSL_free(ci->ssl_comm);
//удаление из списка
	if (ci->prev)
		ci->prev->next = ci->next;
	else
		client_list_head = ci->next;

	if (ci->next)
		ci->next->prev = ci->prev;
	else
		client_list_end = ci->prev;

//удаление из дерева
	if (client_tree_search(ci->id) == ci)
	{
		if (ci->next_multi)
			client_tree_replace_multi(ci);
		else
			client_tree_remove(ci);
		client_tree_print(client_tree_root);
	}

	cclink_list_delete_by_client(ci);

	if (ci->r_buf)
		free(ci->r_buf);

	if (ci->w_buf)
		free(ci->w_buf);

	CLOSE(ci->sck_comm);
	PICA_debug2("closed c2n socket %i", ci->sck_comm);

	free(ci);
}

struct PICA_proto_msg* client_wbuf_push
(struct client *c, unsigned int msgid, unsigned int size)
{
	if (!c->w_buf)
	{
		c->w_buf = calloc(1, DEFAULT_BUF_SIZE + (size / DEFAULT_BUF_SIZE) * DEFAULT_BUF_SIZE );
		if (!c->w_buf)
		{
			PICA_error("calloc() failed. Out of memory");
			return 0;
		}
		c->buflen_w = DEFAULT_BUF_SIZE + (size / DEFAULT_BUF_SIZE) * DEFAULT_BUF_SIZE;
	}

	if ((c->buflen_w - c->w_pos) < size)
	{
		unsigned char *p;
		p = realloc(c->w_buf, size + c->w_pos);

		if (p)
		{
			c->w_buf = p;
			c->buflen_w = size + c->w_pos;
		}
		else
			return 0;
	}

	struct PICA_proto_msg *mp = (struct PICA_proto_msg *)(c->w_buf + c->w_pos);
	mp->head[0] = mp->head[1] = msgid;
	c->w_pos += size;
	return mp;
}

int client_rbuf_grow(struct client *c)
{
	unsigned char *p;

	p = realloc(c->r_buf, c->buflen_r + DEFAULT_BUF_SIZE);

	if (!p)
	{
		PICA_error("realloc() failed");
		return 0;
	}
	c->r_buf = p;
	c->buflen_r += DEFAULT_BUF_SIZE;

	return 1;
}

struct nodelink *nodelink_list_addnew(struct newconn *nc)
{
	struct nodelink *nl;

	nl = (struct nodelink*)calloc(1, sizeof(struct nodelink));

	if (!nl)
		return 0;

	if (nodelink_list_end)
	{
		nodelink_list_end->next = nl;
		nl->prev = nodelink_list_end;
		nodelink_list_end = nl;
	}
	else
	{
		nodelink_list_head = nl;
		nodelink_list_end = nl;
	}

	nl->sck = nc->sck;
	nl->addr = nc->addr;
	nl->anonssl = nc->anonssl;

	nl->r_buf = calloc(1, DEFAULT_BUF_SIZE);
	if (!nl->r_buf)
	{
		free(nl);
		return 0;
	}
	nl->buflen_r = DEFAULT_BUF_SIZE;

	nl->tmst = time(0);

	nodelink_list_count++;
	return nl;
}

void nodelink_list_delete(struct nodelink *n)
{
	PICA_debug1("nodelink_list_delete(%p)", (void*)n);

	if (n->r_buf)
		free(n->r_buf);

	if (n->w_buf)
		free(n->w_buf);

	if (n->node_addr)
		free(n->node_addr);

	SSL_free(n->anonssl);

	CLOSE(n->sck);
	PICA_debug2("closed n2n socket %i", n->sck);

	if (n->prev)
		n->prev->next = n->next;
	else
		nodelink_list_head = n->next;

	if (n->next)
		n->next->prev = n->prev;
	else
		nodelink_list_end = n->prev;

	cclink_list_delete_by_nodelink(n);

	nodelink_list_count--;
	free(n);
}

int nodelink_attach_nodeaddr(struct nodelink *nl, unsigned int addr_type, void* nodeaddr, unsigned int nodeaddr_size)
{
	if (nl && nodeaddr)
	{
		if (nl->node_addr)
			free(nl->node_addr);

		nl->node_addr = malloc(nodeaddr_size);
		if (!nl->node_addr)
			return 0;//ERR_CHECK
		memcpy(nl->node_addr, nodeaddr, nodeaddr_size);

		nl->addr_type = addr_type;
		return 1;
	}
	return 0;
}

int nodelink_rbuf_grow(struct nodelink *nl)
{
	unsigned char *p;

	p = realloc(nl->r_buf, nl->buflen_r + DEFAULT_BUF_SIZE);

	if (!p)
	{
		PICA_error("realloc() failed");
		return 0;
	}
	nl->r_buf = p;
	nl->buflen_r += DEFAULT_BUF_SIZE;

	return 1;
}

struct PICA_proto_msg* nodelink_wbuf_push
(struct nodelink *nl, unsigned int msgid, unsigned int size)
{
	struct PICA_proto_msg *mp;

	if (!nl->w_buf)
	{
		nl->w_buf = calloc(1, DEFAULT_BUF_SIZE );
		if (!nl->w_buf)
		{
			PICA_error("calloc() failed. Out of memory");
			return 0;
		}
		nl->buflen_w = DEFAULT_BUF_SIZE;
	}

	if ((nl->buflen_w - nl->w_pos) < size)
	{
		unsigned char *p;
		p = realloc(nl->w_buf, size + nl->w_pos);

		if (p)
		{
			nl->w_buf = p;
			nl->buflen_w = size + nl->w_pos;
		}
		else
			return 0;
	}

	mp = (struct PICA_proto_msg *)(nl->w_buf + nl->w_pos);
	mp->head[0] = mp->head[1] = msgid;
	nl->w_pos += size;
	return mp;
}

struct nodelink *nodelink_search_by_ipv4addr(in_addr_t ip, in_port_t port)
{
	struct nodelink *nl;

	nl = nodelink_list_head;

	while(nl && !(
	            (nl->addr.sin_addr.s_addr == ip && nl->addr.sin_port == port) ||
	            (nl->node_addr && nl->addr_type == PICA_PROTO_NEWNODE_IPV4 &&
	             ((struct PICA_nodeaddr_ipv4*)nl->node_addr) ->addr == ip &&
	             ((struct PICA_nodeaddr_ipv4*)nl->node_addr) ->port == port
	            )
	        )
	     )
	{
		nl = nl->next;
	}

	return nl;
}


void process_timeouts_newconn()
{
	clock_t t;
	int i;

	t = time(0);

	for (i = 0; i < MAX_NEWCONNS; i++)
	{
		if (newconns[i].sck >= 0)
		{
			if (newconns[i].tmst < t)
			{
				if (t - newconns[i].tmst > NEWCONN_TIMEOUT)
				{
					newconn_close(newconns + i);
				}
			}
			else if ( newconns[i].tmst - t > NEWCONN_TIMEOUT)
			{
				newconn_close(newconns + i);
			}
		}
	}
}

void process_timeouts_c2c()
{
	struct cclink *il;

	il = cclink_list_head;

	while(il)
	{
		switch(il->state)
		{
		case PICA_CCLINK_LOCAL_ACTIVE:
		case PICA_CCLINK_N2NCLR_ACTIVE:
		case PICA_CCLINK_N2NCLE_ACTIVE:

			//idle timeout (>=60sec), update timestamp at process_c2c

			break;
		default:
			if ((time(0) - il->tmst) > TMO_CCLINK_WAITACTIVE)
			{
				struct cclink *dl;
				dl = il;
				il = il->next;
				cclink_list_delete(dl);
				continue;
			}
		}
		il = il->next;
	}
}

void process_timeouts_c2n()
{
	struct client *i_ptr, *kill_ptr;
	int ret;

	i_ptr = client_list_head;
	kill_ptr = 0;

	clock_t cur_time = time(0);

	while(i_ptr)
	{
		if (i_ptr->disconnect_ticking)
		{
			if (cur_time >= i_ptr->tmst)
			{
				if ((cur_time - i_ptr->tmst) > NOREPLY_TIMEOUT)
					kill_ptr = i_ptr;
			}
			else
			{
				//2038 year has come, and this code is still working somewhere with 32bit Unix timestamps :)
				if ((i_ptr->tmst - cur_time ) > NOREPLY_TIMEOUT)
					kill_ptr = i_ptr;
			}
		}
		else
		{
			if (cur_time >= i_ptr->tmst)
			{
				if ((cur_time - i_ptr->tmst) > KEEPALIVE_TIMEOUT)
				{
					struct PICA_proto_msg *mp;

					if ((mp = client_wbuf_push(i_ptr, PICA_PROTO_PINGREQ, PICA_PROTO_PINGREQ_SIZE)))
					{
						PICA_debug3("sending PINGREQ to %p", i_ptr);
						RAND_pseudo_bytes(mp->tail, 2);

						i_ptr->tmst = time(0);
						i_ptr->disconnect_ticking = 1;
					}
					else
						kill_ptr = i_ptr;
				}
			}
		}

		i_ptr = i_ptr->next;

		if (kill_ptr)
		{
			PICA_info("Disconnecting user %u due to noreply timeout", kill_ptr -> id);
			client_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

void process_timeouts_n2n()
{
	struct nodelink *i_ptr, *kill_ptr;
	int ret;

	i_ptr = nodelink_list_head;
	kill_ptr = 0;

	clock_t cur_time = time(0);

	while(i_ptr)
	{
		if (i_ptr->disconnect_ticking)
		{
			if (cur_time >= i_ptr->tmst)
			{
				if ((cur_time - i_ptr->tmst) > NOREPLY_TIMEOUT)
					kill_ptr = i_ptr;
			}
			else
			{
				//2038 year has come, and this code is still working somewhere with 32bit Unix timestamps :)
				if ((i_ptr->tmst - cur_time ) > NOREPLY_TIMEOUT)
					kill_ptr = i_ptr;
			}
		}
		else
		{
			if (cur_time >= i_ptr->tmst)
			{
				if ((cur_time - i_ptr->tmst) > KEEPALIVE_TIMEOUT)
				{
					struct PICA_proto_msg *mp;

					if ((mp = nodelink_wbuf_push(i_ptr, PICA_PROTO_PINGREQ, PICA_PROTO_PINGREQ_SIZE)))
					{
						PICA_debug3("sending PINGREQ to node %p", i_ptr);
						RAND_pseudo_bytes(mp->tail, 2);

						i_ptr->tmst = time(0);
						i_ptr->disconnect_ticking = 1;
					}
					else
						kill_ptr = i_ptr;
				}
			}
		}

		i_ptr = i_ptr->next;

		if (kill_ptr)
		{
			PICA_info("Disconnecting node %p due to noreply timeout", kill_ptr);
			nodelink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

int process_timeouts()
{
	process_timeouts_newconn();
	process_timeouts_c2n();
	process_timeouts_c2c();
	process_timeouts_n2n();

	if (time(0) - skynet_refresh_tmst > SKYNET_REFRESH_TIMEOUT && !nodewait_list)
	{
		PICA_debug3("refreshing nodelist database");
		PICA_node_joinskynet(nodecfg.nodes_db_file, nodecfg.announced_addr);
	}
	return 1;
}

void set_select_fd(SOCKET s, fd_set *readfds, int *nfds)
{
	FD_SET(s, readfds);

	if (s > *nfds)
		*nfds = s;
}

void listen_set_fds(fd_set *readfds, int *nfds)
{
	set_select_fd(listen_comm_sck, readfds, nfds);
}

void newconn_set_fds(fd_set *readfds, int *nfds)
{
	int i;

	for (i = 0; i < MAX_NEWCONNS; i++)
		if (newconns[i].sck >= 0)
			set_select_fd(newconns[i].sck, readfds, nfds);
}

void c2n_set_fds(fd_set *readfds, int *nfds)
{
	struct client *i_ptr;

	i_ptr = client_list_head;
	while(i_ptr)
	{
		set_select_fd(i_ptr->sck_comm, readfds, nfds);

		i_ptr = i_ptr->next;
	}
}

void c2c_set_fds(fd_set *readfds, int *nfds)
{
	struct cclink *cc;

	cc = cclink_list_head;
	while(cc)
	{
		if ( !cc->jam_p1p2 && (cc->state == PICA_CCLINK_LOCAL_ACTIVE || cc->state == PICA_CCLINK_N2NCLR_ACTIVE) )
			set_select_fd(cc->sck_p1, readfds, nfds);

		if ( !cc->jam_p2p1 && (cc->state == PICA_CCLINK_LOCAL_ACTIVE || cc->state == PICA_CCLINK_N2NCLE_ACTIVE) )
			set_select_fd(cc->sck_p2, readfds, nfds);

		cc = cc->next;
	}
}

void n2n_set_fds(fd_set *readfds, int *nfds)
{
	struct nodelink *nl;
	nl = nodelink_list_head;

	while(nl)
	{
		set_select_fd(nl->sck, readfds, nfds);
		nl = nl->next;
	}
}

void process_listen_read(fd_set *readfds)
{
	struct newconn *nc;
	SOCKET s;
	struct sockaddr_in addr;
	int addrsize = sizeof(struct sockaddr_in);
	if (FD_ISSET(listen_comm_sck, readfds))
	{
		s = accept(listen_comm_sck, (struct sockaddr*)&addr, &addrsize);

		if (s >= 0)
		{
			nc = newconn_add(newconns, &newconns_pos);

			nc->anonssl = SSL_new(anon_ctx);

			if (!nc->anonssl)
			{
				PICA_error("failed to allocate SSL struct");
				return;
			}

			if (!SSL_set_fd(nc->anonssl, s))
			{
				SSL_free(nc->anonssl);
				return;
			}


			nc->sck = s;
			nc->addr = addr;
			nc->state = PICA_NEWCONN_WAITANONTLSACCEPT;

			IOCTLSETNONBLOCKINGSOCKET(s, 1);

			PICA_debug1("accepted connection from %.16s, new socket %i", inet_ntoa(addr.sin_addr), s);//IPv6
		}
		else
		{
			PICA_error("unable to accept connection: %s", strerror(errno));
		}
	}

//IPv6
}

void process_newconn_write()
{
	int i, ret;

	for (i = 0; i < MAX_NEWCONNS; i++)
		if (newconns[i].sck >= 0)
		{

			switch(newconns[i].state)
			{
			case PICA_NEWCONN_SENDINGOK:
				PICA_debug3("sending OK response to newconn on socket %i...\n", newconns[i].sck);
				ret = SSL_write(newconns[i].anonssl, msg_INITRESPOK, sizeof msg_INITRESPOK);

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret <= 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}

				if (ret == sizeof msg_INITRESPOK)
				{
					if (newconns[i].type == NEWCONN_N2N)
					{
						if (!nodelink_list_addnew(&newconns[i]))
						{
							SSL_free(newconns[i].anonssl);
							newconn_close(&newconns[i]);
						}

						newconn_free(&newconns[i]);
					}
					else
						newconns[i].state = PICA_NEWCONN_WAITANONTLSSHUTDOWN;
				}
				break;

			case PICA_NEWCONN_SENDINGREJECT:
				PICA_debug3("sending reject response to newconn on socket %i...\n", newconns[i].sck);
				ret = SSL_write(newconns[i].anonssl, msg_VERDIFFER, sizeof msg_VERDIFFER);

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret <= 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}

				SSL_shutdown(newconns[i].anonssl);
				SHUTDOWN(newconns[i].sck);
				SSL_free(newconns[i].anonssl);
				newconn_close(&newconns[i]);
			}

		}

}

void process_newconn_read(fd_set *readfds)
{
	int i, ret;

	for (i = 0; i < MAX_NEWCONNS; i++)
		if (newconns[i].sck >= 0 && FD_ISSET(newconns[i].sck, readfds))
		{

			switch(newconns[i].state)
			{
			case PICA_NEWCONN_WAITANONTLSACCEPT:
				ret = SSL_accept(newconns[i].anonssl);

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret <= 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}
				else if (ret == 1)
				{
					newconns[i].state = PICA_NEWCONN_WAITREQUEST;
				}

			case PICA_NEWCONN_WAITREQUEST:
				ret = SSL_read(newconns[i].anonssl, newconns[i].buf + newconns[i].pos, NEWCONN_BUFSIZE - newconns[i].pos);

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret <= 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}

				newconns[i].pos += ret;

				if(!PICA_processdatastream(newconns[i].buf, &(newconns[i].pos), newconns + i, _msginfo_newconn, MSGINFO_MSGSNUM(_msginfo_newconn) ))
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
				}

				break;

			/*case PICA_NEWCONN_SENDINGOK:
				PICA_debug3("sending OK response to newconn on socket %i...\n", newconns[i].sck);
				ret = SSL_write(newconns[i].anonssl, msg_INITRESPOK, sizeof msg_INITRESPOK);

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret <= 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}

				if (ret == sizeof msg_INITRESPOK)
				{
					if (newconns[i].type == NEWCONN_N2N)
					{
						if (!nodelink_list_addnew(&newconns[i]))
						{
							SSL_free(newconns[i].anonssl);
							newconn_close(&newconns[i]);
						}

						newconn_free(&newconns[i]);
					}
					else
						newconns[i].state = PICA_NEWCONN_WAITANONTLSSHUTDOWN;
				}*/

			case PICA_NEWCONN_WAITANONTLSSHUTDOWN:
				PICA_debug3("waiting for anon TLS shutdown on socket %i...\n", newconns[i].sck);
				ret = SSL_shutdown(newconns[i].anonssl);

				if (ret == 0)
					break;

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret < 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}

				//switch state of client or cclink on successful shutdown
				if (ret == 1)
				{
					if (newconns[i].iconn.cl && newconns[i].type == NEWCONN_C2N)
					{
						newconns[i].iconn.cl->state = PICA_CLSTATE_TLSNEGOTIATION;
						newconn_free(&newconns[i]);
						break;
					}

					if (newconns[i].iconn.cc && newconns[i].type == NEWCONN_C2C)
					{
						cclink_sendcleconnected(newconns[i].iconn.cc);
						cclink_activate(newconns[i].iconn.cc);

						newconn_free(&newconns[i]);
						break;
					}
				}
				break;

			/*case PICA_NEWCONN_SENDINGREJECT:
				ret = SSL_write(newconns[i].anonssl, msg_VERDIFFER, sizeof msg_VERDIFFER);

				if (ret < 0 && process_async_ssl_readwrite(newconns[i].anonssl, ret))
					break;

				if (ret <= 0)
				{
					SSL_free(newconns[i].anonssl);
					newconn_close(&newconns[i]);
					break;
				}

				SSL_shutdown(newconns[i].anonssl);
				SHUTDOWN(newconns[i].sck);
				SSL_free(newconns[i].anonssl);
				newconn_close(&newconns[i]);*/
			}

		}
}



void process_c2n_read(fd_set *readfds)
{
	struct client *i_ptr, *kill_ptr;
	int ret;

	i_ptr = client_list_head;
	kill_ptr = 0;

	while (i_ptr)
	{
		if (FD_ISSET(i_ptr->sck_comm, readfds))
			switch(i_ptr->state)
			{
			case PICA_CLSTATE_CONNECTED:
			case PICA_CLSTATE_MULTILOGIN_SECONDARY:
				ret = SSL_read(i_ptr->ssl_comm, i_ptr->r_buf + i_ptr->r_pos, i_ptr->buflen_r - i_ptr->r_pos);

				if (!ret)
					kill_ptr = i_ptr;

				if (ret < 0)
					switch(SSL_get_error(i_ptr->ssl_comm, ret))
					{
					case SSL_ERROR_WANT_WRITE:
					case SSL_ERROR_WANT_READ:
						break;
					default:
						kill_ptr = i_ptr;
					}

				if (ret > 0)
				{
					const struct PICA_msginfo *msgs = _msginfo_comm;
					unsigned int nmsgs = MSGINFO_MSGSNUM(_msginfo_comm);

					if (i_ptr->state == PICA_CLSTATE_MULTILOGIN_SECONDARY)
					{
						msgs = _msginfo_multi;
						nmsgs = MSGINFO_MSGSNUM(_msginfo_multi);
					}

					i_ptr->r_pos += ret;
					if(!PICA_processdatastream(i_ptr->r_buf, &(i_ptr->r_pos), i_ptr, msgs, nmsgs))
						kill_ptr = i_ptr;

					if (i_ptr->buflen_r == i_ptr->r_pos)
						if (!client_rbuf_grow(i_ptr))
							kill_ptr = i_ptr;

				}
				break;
			case PICA_CLSTATE_TLSNEGOTIATION:
				i_ptr->w_pos = 1; //????
				//-------------- <<<<<<<<<<<<<<<<<<<<<<<<----------------------------------------
				break;
			};

		i_ptr = i_ptr->next;

		if (kill_ptr)
		{
			client_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}

}

void process_c2c_read(fd_set *readfds)
{
	int i, ret;
	struct cclink *cc, *kill_ptr;

	cc = cclink_list_head;
	kill_ptr = 0;

	while(cc)
	{
		if ((cc->state == PICA_CCLINK_LOCAL_ACTIVE || cc->state == PICA_CCLINK_N2NCLR_ACTIVE)
		        && FD_ISSET(cc->sck_p1, readfds) && !cc->jam_p1p2 )
		{
			ret = recv(cc->sck_p1, cc->buf_p1p2 + cc->bufpos_p1p2, cc->buflen_p1p2 - cc->bufpos_p1p2, 0);
			PICA_debug3("process_c2c_read: recv  p1p2: ret=%i", ret);
			if (ret > 0)
			{
				cc->bufpos_p1p2 += ret;
			}
			else
				kill_ptr = cc;
		}

		if ((cc->state == PICA_CCLINK_LOCAL_ACTIVE || cc->state == PICA_CCLINK_N2NCLE_ACTIVE)
		        && FD_ISSET(cc->sck_p2, readfds)  && !cc->jam_p2p1 )
		{
			ret = recv(cc->sck_p2, cc->buf_p2p1 + cc->bufpos_p2p1, cc->buflen_p2p1 - cc->bufpos_p2p1, 0);
			PICA_debug3("process_c2c_read: recv  p2p1: ret=%i\n", ret);
			if (ret > 0)
			{
				cc->bufpos_p2p1 += ret;
			}
			else
				kill_ptr = cc;
		}


		cc = cc->next;

		if (kill_ptr)
		{
			cclink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

void process_n2n_read(fd_set *readfds)
{
	int i, ret;
	struct nodelink *nl, *kill_ptr;

	nl = nodelink_list_head;
	kill_ptr = 0;

	while(nl)
	{
		if (FD_ISSET(nl->sck, readfds))
		{
			ret = SSL_read(nl->anonssl, nl->r_buf + nl->r_pos, nl->buflen_r - nl->r_pos);

			if (ret > 0)
			{
				nl->r_pos += ret;

				if(!PICA_processdatastream(nl->r_buf, &(nl->r_pos), nl, _msginfo_node, MSGINFO_MSGSNUM(_msginfo_node) ))
					kill_ptr = nl;

				if (nl->buflen_r == nl->r_pos)
					if (!nodelink_rbuf_grow(nl))
						kill_ptr = nl;
			}
			else
			{
				if (ret == 0 || process_async_ssl_readwrite(nl->anonssl, ret) == 0)
				{
					PICA_debug3("SSL_read() failed in process_n2n_read()");
					kill_ptr = nl;
				}
			}
		}

		nl = nl->next;

		if (kill_ptr)
		{
			nodelink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

void process_c2n_write()
{
	struct client *i_ptr, *kill_ptr;
	int ret;

	i_ptr = client_list_head;
	kill_ptr = 0;

	while(i_ptr)
	{
		//---------
		switch(i_ptr->state)
		{
		case PICA_CLSTATE_CONNECTED:
			if (!i_ptr->w_pos)
				break;

			if (!i_ptr->btw_ssl)
				i_ptr->btw_ssl = i_ptr->w_pos;

			ret = SSL_write(i_ptr->ssl_comm, i_ptr->w_buf, i_ptr->btw_ssl);

			PICA_debug3("process_c2n_write:  ret of SSL_write()=%i", ret);

			if (ret == i_ptr->btw_ssl)
			{
				if (ret < i_ptr->w_pos)
					memmove(i_ptr->w_buf, i_ptr->w_buf + ret, i_ptr->w_pos - ret); //may be it's better to implement ring buffer here

				i_ptr->w_pos -= i_ptr->btw_ssl;
				i_ptr->btw_ssl = 0;
			}

			if (!ret)
				kill_ptr = i_ptr;

			if (ret < 0)
			{
				ret = SSL_get_error(i_ptr->ssl_comm, ret);
				if (ret != SSL_ERROR_WANT_WRITE && ret != SSL_ERROR_WANT_READ)
					kill_ptr = i_ptr;
			}
			break;
		case PICA_CLSTATE_WAITANONTLSSHUTDOWN:
			// do nothing here, state will be switched by newconn
			break;
/*		case PICA_CLSTATE_SENDINGRESP:
			ret = send(i_ptr->sck_comm, i_ptr->w_buf, i_ptr->w_pos, 0);
			if (!ret)
				kill_ptr = i_ptr;

			if (ret == SOCKET_ERROR)
				;//ERR_CHECK

			if (ret < i_ptr->w_pos)
				memmove(i_ptr->w_buf, i_ptr->w_buf + ret, i_ptr->w_pos - ret); //ring buffer is better (??)

			i_ptr->w_pos -= ret;


			if (!i_ptr->w_pos)
				i_ptr->state = PICA_CLSTATE_TLSNEGOTIATION;

			break;*/
		case PICA_CLSTATE_TLSNEGOTIATION:
			i_ptr->w_pos = 0;
			ret = SSL_connect(i_ptr->ssl_comm);

			PICA_debug3("process_c2n_write: SSL_connect()=%i", ret);

			if (!ret)
				kill_ptr = i_ptr;

			if (ret < 0)
			{
				int eret;
				eret = SSL_get_error(i_ptr->ssl_comm, ret);
				switch(eret)
				{
				case SSL_ERROR_WANT_WRITE:
					i_ptr->w_pos = 1;
					PICA_debug3("SSL_ERROR_WANT_WRITE");
					break;
				case SSL_ERROR_WANT_READ:
					PICA_debug3("SSL_ERROR_WANT_READ");
					break;
				default:
					PICA_debug3("SSL_get_error() = %i", eret);
					kill_ptr = i_ptr;
				}
			}
			if (ret == 1)
				//проверить сертификат пользователя
			{
				X509* client_cert;
				struct client *primary;

				PICA_info("SSL c2n connection established using %s cipher", SSL_get_cipher(i_ptr->ssl_comm));

				client_cert = SSL_get_peer_certificate (i_ptr->ssl_comm);
				if (!client_cert)
				{
					//ERR_CHECK //нет сертификата
					kill_ptr = i_ptr;
				}

				ret = PICA_id_from_X509(client_cert, i_ptr->id);

				if (ret == 0)
				{
					PICA_error("Unable to get client id from certificate");
					kill_ptr = i_ptr;
				}

				X509_free(client_cert);

				//reject all-zeroes ID
				{
					static const char zeroID[PICA_ID_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
					if (memcmp(zeroID, i_ptr->id, PICA_ID_SIZE) == 0)
					{
						PICA_error("Rejecting all-zero Pica ID");
						kill_ptr = i_ptr;
					}
				}

				if (!kill_ptr && (primary = client_tree_search(i_ptr->id)))
				{
					//PICA_info("Disconnecting client %p because other user with same ID = %u is already connected", i_ptr, i_ptr->id);
					//memset(i_ptr->id, 0, PICA_ID_SIZE);//reset id to all zeros to prevent removing existing ID from previous connection from client_tree
					//ret = 0;//id already exists in tree
					i_ptr->state = PICA_CLSTATE_MULTILOGIN_SECONDARY;
					client_attach_multi_secondary(primary, i_ptr);
				}
				else
				{
					ret = client_tree_add(i_ptr);
					i_ptr->state = PICA_CLSTATE_CONNECTED;
					client_tree_print(client_tree_root);
				}
				if (!ret)
				{
					//ERR_CHECK  //не уникальный id
					kill_ptr = i_ptr;
				}
			}
			break;
		}
		//---------

		i_ptr = i_ptr->next;

		if (kill_ptr)
		{
			client_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

void process_c2c_write()
{
	int i, ret;
	struct cclink *cc, *kill_ptr;

	cc = cclink_list_head;
	kill_ptr = 0;

	while(cc)
	{
		if (cc->bufpos_p1p2/* && (cc->state==PICA_CCLINK_LOCAL_ACTIVE || cc->state==PICA_CCLINK_N2NCLE_ACTIVE)*/)
		{
			if (cc->state == PICA_CCLINK_N2NCLR_ACTIVE) //receiver is on remote node
			{
				int sendlen;
				struct PICA_proto_msg *mp;

				sendlen = (cc->bufpos_p1p2 > PICA_PROTO_N2NMSG_MAXDATASIZE) ? PICA_PROTO_N2NMSG_MAXDATASIZE : cc->bufpos_p1p2;

				if(mp = nodelink_wbuf_push(cc->callee_node, PICA_PROTO_N2NMSG, sendlen + 4 + 2 * PICA_ID_SIZE))
				{
					*((unsigned short*)mp->tail) = 2 * PICA_ID_SIZE + sendlen;
					memcpy(mp->tail + 2, cc->caller_id, PICA_ID_SIZE);
					memcpy(mp->tail + 2 + PICA_ID_SIZE, cc->callee_id, PICA_ID_SIZE);
					memcpy(mp->tail + 2 + 2 * PICA_ID_SIZE, cc->buf_p1p2, sendlen);
					ret = sendlen;
				}
				else
					ret = 0;
			}
			else
				ret = send(cc->sck_p2, cc->buf_p1p2, cc->bufpos_p1p2, MSG_NOSIGNAL);

			PICA_debug3("process_c2c_write: send  p1p2: ret=%i", ret);
			if (ret > 0)
			{
				if (ret < cc->bufpos_p1p2)
					memmove(cc->buf_p1p2, cc->buf_p1p2 + ret, cc->bufpos_p1p2 - ret); //may be it's better to implement ring buffer here
				cc->bufpos_p1p2 -= ret;
				cc->jam_p1p2 = 0;
			}

			if (ret == 0)
			{
				kill_ptr = cc;
			}
			if (ret == -1)
			{
#ifdef WIN32
				ret = WSAGetLastError();
				if (ret == WSAEWOULDBLOCK || ret == WSAENOBUFS)
					cc->jam_p1p2 = 1;
				else
				{
					kill_ptr = cc;
				}
#else
				ret = errno;
				if (ret == EAGAIN || ret == ENOBUFS)
					cc->jam_p1p2 = 1;
				else
				{
					kill_ptr = cc;
				}
#endif
			}

		}
		if (cc->bufpos_p2p1 /*&& (cc->state==PICA_CCLINK_LOCAL_ACTIVE || cc->state==PICA_CCLINK_N2NCLR_ACTIVE)*/)
		{
			if (cc->state == PICA_CCLINK_N2NCLE_ACTIVE) //receiver is on remote node
			{
				int sendlen;
				struct PICA_proto_msg *mp;

				sendlen = (cc->bufpos_p2p1 > PICA_PROTO_N2NMSG_MAXDATASIZE) ? PICA_PROTO_N2NMSG_MAXDATASIZE : cc->bufpos_p2p1;

				if(mp = nodelink_wbuf_push(cc->caller_node, PICA_PROTO_N2NMSG, sendlen + 4 + 2 * PICA_ID_SIZE))
				{
					*((unsigned short*)mp->tail) = 2 * PICA_ID_SIZE + sendlen;
					memcpy(mp->tail + 2, cc->callee_id, PICA_ID_SIZE);
					memcpy(mp->tail + 2 + PICA_ID_SIZE, cc->caller_id, PICA_ID_SIZE);
					memcpy(mp->tail + 2 + 2 * PICA_ID_SIZE, cc->buf_p2p1, sendlen);
					ret = sendlen;
				}
				else
					ret = 0;
			}
			else
				ret = send(cc->sck_p1, cc->buf_p2p1, cc->bufpos_p2p1, MSG_NOSIGNAL);

			PICA_debug3("process_c2c_write: send  p2p1: ret=%i", ret);
			if (ret > 0)
			{
				if (ret < cc->bufpos_p2p1)
					memmove(cc->buf_p2p1, cc->buf_p2p1 + ret, cc->bufpos_p2p1 - ret); //may be it's better to implement ring buffer here
				cc->bufpos_p2p1 -= ret;
				cc->jam_p2p1 = 0;
			}

			if (ret == 0)
			{
				kill_ptr = cc;
			}
			if (ret == -1)
			{
#ifdef WIN32
				ret = WSAGetLastError();
				if (ret == WSAEWOULDBLOCK || ret == WSAENOBUFS)
					cc->jam_p2p1 = 1;
				else
				{
					kill_ptr = cc;
				}
#else
				ret = errno;
				if (ret == EAGAIN || ret == ENOBUFS)
					cc->jam_p2p1 = 1;
				else
				{
					kill_ptr = cc;
				}
#endif

			}

		}

		cc = cc->next;

		if (kill_ptr)
		{
			cclink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

void process_n2n_write()
{
	int ret;
	struct nodelink *nl, *kill_ptr;

	nl = nodelink_list_head;
	kill_ptr = 0;

	while(nl)
	{
		if (nl->w_pos > 0)
		{
			if (!nl->btw_ssl)
				nl->btw_ssl = nl->w_pos;

			ret = SSL_write(nl->anonssl, nl->w_buf, nl->btw_ssl);

			if (ret == nl->btw_ssl)
			{
				if (ret < nl->w_pos)
					memmove(nl->w_buf, nl->w_buf + ret, nl->w_pos - ret); //may be it's better to implement ring buffer here
				nl->w_pos -= nl->btw_ssl;
				nl->btw_ssl = 0;
			}

			if (ret == 0 || ret < 0 && process_async_ssl_readwrite(nl->anonssl, ret) == 0)
			{
				kill_ptr = nl;
			}

		}

		nl = nl->next;

		if (kill_ptr)
		{
			nodelink_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}
}

void process_nodewait()
{
	struct nodewait *kill_ptr = 0, *nw = nodewait_list;

	while(nw)
	{
		//PICA_debug3("checking nodewait pointer %p", nw);
		switch(nw->state)
		{
		case PICA_NODEWAIT_RESOLVED:
		{
			int skip = 0;
			struct nodelink *nl;

			nl = nodelink_list_head;

			while(nl && !skip)//check existing connections
			{
				struct addrinfo *p_ai = nw->ai;

				while(p_ai) //IPv6
				{
					struct sockaddr_in in = *(struct sockaddr_in*)p_ai->ai_addr;

					if (nl->addr.sin_family == in.sin_family && nl->addr.sin_addr.s_addr == in.sin_addr.s_addr && nl->addr.sin_port == in.sin_port)
					{
						skip = 1;
						break;
					}

					p_ai = p_ai->ai_next;
				}
				nl = nl->next;
			}

			if (!skip)
				nodewait_start_connect(nw);
			else
			{
				PICA_debug3("skipping node %.255s %u , connection already exist", nw->addr.addr, nw->addr.port);
				kill_ptr = nw;
			}
		}
		break;

		case PICA_NODEWAIT_CONNECTED:
		{
			struct nodelink *nlp;

			PICA_debug1("connected to %.255s %u, sending request ", nw->addr.addr, nw->addr.port);
			PICA_debug2("connection socket is %i", nw->nc.sck);
			nlp = nodelink_list_addnew(&nw->nc);
			if (nlp)
			{
				struct PICA_proto_msg *mp;
				mp = nodelink_wbuf_push(nlp, PICA_PROTO_NODECONNREQ, PICA_PROTO_NODECONNREQ_SIZE);
				if (mp)
				{
					mp->tail[0] = PICA_PROTO_VER_HIGH;
					mp->tail[1] = PICA_PROTO_VER_LOW;
				}
			}
			PICA_nodeaddr_update(nodecfg.nodes_db_file, &nw->addr, 1);
			kill_ptr = nw;
		}
		break;

		case PICA_NODEWAIT_RESOLVING_FAILED:
		{
			PICA_debug1("resolving %.255s %u failed: %s", nw->addr.addr, nw->addr.port, gai_strerror(nw->ai_errorcode));
			PICA_nodeaddr_update(nodecfg.nodes_db_file, &nw->addr, 0);
			kill_ptr = nw;
		}
		break;

		case PICA_NODEWAIT_CONNECT_FAILED:
		{
			PICA_debug1("connection to %.255s %u failed", nw->addr.addr, nw->addr.port);
			PICA_debug2("socket %i is closed", nw->nc.sck);
			PICA_nodeaddr_update(nodecfg.nodes_db_file, &nw->addr, 0);
			kill_ptr = nw;
		}
		break;
		}

		nw = nw->next;

		if (kill_ptr)
		{
			nodewait_list_delete(kill_ptr);
			kill_ptr = 0;
		}
	}

}

int node_loop()
{
	fd_set fds;
	struct timeval tv;
	int nfds, ret;

	do
	{
		nfds = 0;

		FD_ZERO(&fds);
		tv.tv_sec = SELECT_TIMEOUT;
		tv.tv_usec = 0;

		listen_set_fds(&fds, &nfds);
		newconn_set_fds(&fds, &nfds);
		c2n_set_fds(&fds, &nfds);
		c2c_set_fds(&fds, &nfds);
		n2n_set_fds(&fds, &nfds);

		ret = select(nfds + 1, &fds, 0, 0, &tv);

		if (ret < 0)
		{
			perror("node_loop-select error ");
			//ERR_CHECK
			return -1;
		}

		if (ret > 0)
		{
			process_c2n_read(&fds);
			process_c2c_read(&fds);
			process_n2n_read(&fds);
			process_newconn_read(&fds);
			process_listen_read(&fds);
		}


		process_c2n_write();
		process_c2c_write();
		process_n2n_write();
		process_newconn_write();

		process_timeouts();
		process_nodewait();
	}
	while(1);////<<<<<<<<<<
	return 0;
}

void print_usage()
{
	puts("Usage: pica-node [-f config-file]\n\
\n\
Options:\n\
	-f 	configuration file\n\
	-h	show this text\n\
	-v	verbose\n\
	-V	print version info\n");
}

void print_version()
{
	puts("Pica Pica Node v"PACKAGE_VERSION" \nCopyright  (c) 2012 - 2018 Anton Sviridenko\n");
	puts("protocol version "PICA_PROTO_VER_STRING"\n");
	puts("License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n");

////----include this only if OpenSSL is statically linked
	puts("This product includes software developed by the OpenSSL Project\n\
for use in the OpenSSL Toolkit (http://www.openssl.org/)\n");
	puts("This product includes cryptographic software written by\n\
Eric Young (eay@cryptsoft.com)\n");
	puts(SSLeay_version(SSLEAY_VERSION));
////---


	puts("\nThis product includes iniParser\n\
Copyright (c) 2000-2012 by Nicolas Devillard.\n\
MIT License");

}

void process_cmdline(int argc, char** argv)
{
	int opt;
	int verbosity = 0;

	while((opt = getopt(argc, argv, "f:hvV")) != -1)
	{
		switch(opt)
		{
		case 'f':
			nodecfg.config_file = strdup(optarg);
			break;

		case 'h':
			print_usage();
			exit(0);
			break;

		case 'v':
			verbosity++;

			break;

		case 'V':
			print_version();
			exit(0);
			break;

		default:
			PICA_error("unknown option - %c", optopt);
			print_usage();
			exit(-1);
		}
	}

	if (verbosity > 3)
		verbosity = 3;

	PICA_set_loglevel(PICA_LOG_INFO + verbosity);
}

void PICA_node_joinskynet(const char* addrlistfilename, const char *my_addr)
{
	struct PICA_nodeaddr *nap, *addrlist_h = 0;
	struct nodelink *nl;
	int ret;

	skynet_refresh_tmst = time(0);

	ret = PICA_nodeaddr_list_load(addrlistfilename, &addrlist_h);//MEM
	if (ret <= 0)
		return;

	nap = addrlist_h;

	while(nap)
	{
		int skip = 0;

		if (0 == strncmp(nap->addr, my_addr, 256) && nap->port == atoi(nodecfg.listen_port))
		{
			PICA_debug3("Skipping self address %.255s port %u", nap->addr, nap->port);
			skip = 1;
		}

		if (nodecfg.disable_reserved_addrs && INADDR_NONE != inet_addr(nap->addr) && PICA_is_reserved_addr_ipv4(inet_addr(nap->addr)))
		{
			PICA_debug3("Skipping IP from private range %.255s port %u", nap->addr, nap->port);
			skip = 1;
		}

		nl = nodelink_list_head;
		//check if connection to node with current address is already established
		while(nl && !skip)
		{
			if (nl->node_addr)
			{
				switch(nl->addr_type)
				{
				case PICA_PROTO_NEWNODE_IPV4:
					if (0 == strncmp(nap->addr, inet_ntoa(*(struct in_addr*) & (((struct PICA_nodeaddr_ipv4*)nl->node_addr)->addr)), 16)
					        &&
					        nap->port == ntohs(((struct PICA_nodeaddr_ipv4*)nl->node_addr)->port))
						skip = 1;
					break;
					//IPv6
				}
			}

			if (0 == strncmp(nap->addr, inet_ntoa(nl->addr.sin_addr), 16) && nap->port == ntohs(nl->addr.sin_port))
				skip = 1;


			nl = nl->next;
		}

		if (!skip)
			nodewait_start_resolve(nap);

		nap = nap->next;
	}

	PICA_nodeaddr_list_free(addrlist_h);
}

int main(int argc, char** argv)
{

	process_cmdline(argc, argv);

	nodecfg.config_file = (nodecfg.config_file ? nodecfg.config_file : PICA_NODECONFIG_DEF_CONFIG_FILE);

	if (!PICA_nodeconfig_load(nodecfg.config_file))
	{
		PICA_fatal("Unable to load configuration. Please check if config file %s exists and has correct permissions", nodecfg.config_file);
	}

	PICA_debug1("nodecfg.config_file = %s", nodecfg.config_file);
	PICA_debug1("nodecfg.announced_addr = %s", nodecfg.announced_addr);
	PICA_debug1("nodecfg.listen_port = %s", nodecfg.listen_port);
	PICA_debug1("nodecfg.nodes_db_file = %s", nodecfg.nodes_db_file);
	PICA_debug1("nodecfg.disable_reserved_addrs = %s", nodecfg.disable_reserved_addrs ? "yes" : "no");

	if (!PICA_node_init())
		return -1;

	if (strcmp("autoconfigure", nodecfg.announced_addr) == 0)
	{
		in_addr_t guess;
		struct in_addr in;

		guess = PICA_guess_listening_addr_ipv4();

		in.s_addr = guess;

		free(nodecfg.announced_addr);
		nodecfg.announced_addr = strdup(inet_ntoa(in));

		PICA_debug1("guessed interface address to be announced: %s", nodecfg.announced_addr);

#ifdef HAVE_LIBMINIUPNPC
		if (PICA_is_reserved_addr_ipv4(guess) && nodecfg.disable_reserved_addrs)
		{
			char public_ip[64];
			int ret;

			PICA_info("trying to get global IP with UPnP...");

			ret = PICA_upnp_autoconfigure_ipv4(atoi(nodecfg.listen_port), atoi(nodecfg.listen_port), public_ip);
			if (ret)
			{
				free(nodecfg.announced_addr);
				nodecfg.announced_addr = strdup(public_ip);
			}
		}
#endif
		PICA_info("autoconfigured announced address %s port %s", nodecfg.announced_addr, nodecfg.listen_port);
	}

	if (INADDR_NONE == inet_addr(nodecfg.announced_addr) || INADDR_ANY == inet_addr(nodecfg.announced_addr))
	{
		PICA_fatal("announced_addr  (%.16s) is invalid or not configured. Please set correct public IP address of your pica-node instance in config file.",
		           nodecfg.announced_addr
		          );
	}

	if (nodecfg.disable_reserved_addrs && PICA_is_reserved_addr_ipv4(inet_addr(nodecfg.announced_addr)))
	{
		PICA_fatal("announced_addr  (%.16s) is in private range and is unacceptable for global Internet. Private ranges are disabled by node configuration.",
		           nodecfg.announced_addr
		          );
	}

	PICA_node_joinskynet(nodecfg.nodes_db_file, nodecfg.announced_addr);//CONF-CONF имя файла с адресами узлов, свой адрес

	my_addr = nodecfg.announced_addr;//TEMP FIXME

	return node_loop();
}
