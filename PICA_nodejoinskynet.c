#define JOINSKYNET
#include "PICA_node.h"
#include "PICA_nodejoinskynet.h"
#include "PICA_proto.h"
#include "PICA_msgproc.h"
#include "PICA_common.h"
#include "PICA_nodeconfig.h"
#include "PICA_log.h"
#include <time.h>
#include <sys/time.h>

#warning "USE HEADER  VVV"
struct nodelink *nodelink_list_addnew(struct newconn *nc);
extern struct PICA_msginfo _msginfo_node[];
struct PICA_proto_msg* nodelink_wbuf_push
	(struct nodelink *nl,unsigned int msgid,unsigned int size);

//Делает попытку подключения к указанному узлу, при удачном подключении отправляет узлу NODECONNREQ и возвращает 1. При неудаче возвращает 0
//static int try_connect_to_node(struct PICA_nodeaddr *nap,struct newconn *nc)
	
/*static*/ int try_connect_to_node(char *addr, unsigned int port,struct newconn *nc)
{
struct addrinfo h,*r,*ap;
char portbuf[8];
char msg[PICA_PROTO_NODECONNREQ_SIZE]=
{PICA_PROTO_NODECONNREQ,PICA_PROTO_NODECONNREQ,
PICA_PROTO_VER_HIGH,PICA_PROTO_VER_LOW};
int ret;

memset(&h,0,sizeof(struct addrinfo));

#ifdef AI_IDN
#define H_AI_FLAGS (AI_ADDRCONFIG | AI_IDN)
#else
#define H_AI_FLAGS AI_ADDRCONFIG
#endif

h.ai_flags=H_AI_FLAGS;
h.ai_family=AF_INET;//CONF
h.ai_socktype=SOCK_STREAM;
h.ai_protocol=IPPROTO_TCP;

sprintf(portbuf,"%u",port);

if (0!=getaddrinfo(addr,portbuf,&h,&r))
	return 0;

ap=r;

while(ap)
	{
	nc->sck=socket(ap->ai_family,ap->ai_socktype,ap->ai_protocol);
	if 	(nc->sck==SOCKET_ERROR)
		{
		ap=ap->ai_next;
		continue;
		}

	if (0!=connect(nc->sck,ap->ai_addr,ap->ai_addrlen))	
		{
		ap=ap->ai_next;

		CLOSE(nc->sck);
		continue;
		}
	
	if (PICA_PROTO_NODECONNREQ_SIZE!=
			send(nc->sck,msg,PICA_PROTO_NODECONNREQ_SIZE,0))
		{
		ap=ap->ai_next;
		CLOSE(nc->sck);
		}
	else
		break;
	}

if (ap)
	{
	nc->addr=*((struct sockaddr_in*)(ap->ai_addr));
	freeaddrinfo(r);
	return 1;
	}

freeaddrinfo(r);
return 0;
}

/*static*/ int try_get_reply(struct newconn *nc)
{
int ret;
fd_set fds;
struct timeval tv={10,0};
union _pv
	{
	int i;
	char c[4];
	} 
proto_version;

proto_version.c[0]=PICA_PROTO_VER_HIGH;
proto_version.c[1]=PICA_PROTO_VER_LOW;
FD_ZERO(&fds);		
FD_SET(nc->sck,&fds);
ret=select(nc->sck+1,&fds,0,0,&tv);
if (0==ret)
	return 0;
if (-1==ret)
	return 0;

ret=recv(nc->sck,nc->buf,4,0);
if (ret<=0)
	return 0;

nc->pos+=ret;
if (!PICA_processdatastream(nc->buf,(unsigned int*)&nc->pos,&proto_version,_msginfo_node,2))
	return 0;
		
if (!proto_version.i) //server reply was ok
	return 1;
else
	{
	if (proto_version.c[0]> PICA_PROTO_VER_HIGH ||  (proto_version.c[1]> PICA_PROTO_VER_LOW && proto_version.c[0]== PICA_PROTO_VER_HIGH))
		{
		PICA_info("make update of node software - peer node has newer protocol version");
		}
	else
		{//peer node has older software version
		PICA_info("peer node has older protocol version");
		}
	}
return 0;
}

/*static*/ void send_newnode(struct nodelink *nlp, const char *my_addr)
{
struct PICA_proto_msg *mp;
mp=nodelink_wbuf_push(nlp,PICA_PROTO_NEWNODE_IPV4,PICA_PROTO_NEWNODE_IPV4_SIZE);
*((in_addr_t*)(mp->tail))=inet_addr(my_addr);//CONF TEMP
*((uint16_t*)(mp->tail+4))=(nodecfg.listen_port ? htons(atoi(nodecfg.listen_port)) :  htons(PICA_COMM_PORT));//CONF; 
}

static void send_nodelistreq(struct nodelink *nlp)
{
struct PICA_proto_msg *mp;
mp=nodelink_wbuf_push(nlp,PICA_PROTO_NODELISTREQ,PICA_PROTO_NODELISTREQ_SIZE);
RAND_bytes(mp->tail, 2); 
}

//Загружает из указанного файла addrlistfilename список адресов узлов, делает попытки  подключения ко всем узлам, после удачного подключения  к какому-либо из них запрашивает у узла актуальный список адресов узлов, после чего возвращает количество активных узлов в сети
int PICA_node_joinskynet(char* addrlistfilename,const char *my_addr)
{
struct PICA_nodeaddr *nap,*addrlist_h=0;
struct nodelink *nlp;
struct newconn nc;
clock_t tmst,t;
int ret;


ret=PICA_nodeaddr_list_load(addrlistfilename,&addrlist_h);//MEM
if (ret<=0)
	return ret;
nap=addrlist_h;
memset(&nc,0,sizeof(nc));
while(nap)
	{
	if (try_connect_to_node(nap->addr,nap->port,&nc))
		if (try_get_reply(&nc))
			{
			nlp=nodelink_list_addnew(&nc);
			send_newnode(nlp,my_addr);
			send_nodelistreq(nlp);
			ret=1;
			break;
			}
			
		
	CLOSE(nc.sck);
	nc.pos=0;
	nap=nap->next;
	}

PICA_nodeaddr_list_free(addrlist_h);
return ret;
}

