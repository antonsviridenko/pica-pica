 
#include "PICA_nodewait.h"
#include "PICA_log.h"
#include "PICA_common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef WIN32

#include <windows.h>

#else

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#endif

struct nodewait *nodewait_list;

struct nodewait *nodewait_list_addnew()
{
struct nodewait *nw;
    
nw = (struct nodewait*)calloc(1, sizeof(struct nodewait));
    
if (!nw)
	return 0;

nw->next = nodewait_list;
nodewait_list = nw;

nw->state = PICA_NODEWAIT_NEW;
return nw;
}

void nodewait_list_delete(struct nodewait *nw)
{
struct nodewait **pp = &nodewait_list;
struct nodewait *p = nodewait_list;

PICA_debug3("nodewait_list_delete(%p)", nw);

while(p) 
	{
	if (p == nw)
		{
		*pp = nw->next;
		//
		//free resources
		//
		free(nw);
	
		return;
		}
		
	pp = &p->next;
	p = p->next;
	}
}

#ifdef WIN32
static DWORD WINAPI nodewait_thread (void *arg)
#else
static void  *nodewait_thread (void *arg)
#endif
{
struct nodewait *nw = (struct nodewait *)arg;
struct addrinfo h,*r,*ap;
char portbuf[8];

nw->state = PICA_NODEWAIT_RUNNING;

memset(&h,0,sizeof(struct addrinfo));

#ifdef AI_IDN
#define H_AI_FLAGS (AI_ADDRCONFIG | AI_IDN)
#else
#define H_AI_FLAGS AI_ADDRCONFIG
#endif

h.ai_flags=H_AI_FLAGS;
h.ai_family=AF_INET;//CONF - IPv4, IPv6, IPv4 then IPv6, IPv6 then IPv4
h.ai_socktype=SOCK_STREAM;
h.ai_protocol=IPPROTO_TCP;

sprintf(portbuf,"%u",nw->addr.port);

if (0!=getaddrinfo(nw->addr.addr, portbuf, &h, &r))
	return 0;

ap = r;

while(ap)
	{
	nw->nc.sck=socket(ap->ai_family,ap->ai_socktype,ap->ai_protocol);
	if 	(nw->nc.sck==SOCKET_ERROR)
		{
		ap=ap->ai_next;
		continue;
		}

	if (0 != connect(nw->nc.sck,ap->ai_addr,ap->ai_addrlen))	
		{
		ap=ap->ai_next;

		CLOSE(nw->nc.sck);
		continue;
		}
	else
		{
		break;
		}
	}

if (ap)
	{
	nw->nc.addr=*((struct sockaddr_in*)(ap->ai_addr));
	freeaddrinfo(r);

	nw->state = PICA_NODEWAIT_FINISHED_OK;
	return 1;
	}

freeaddrinfo(r);
nw->state = PICA_NODEWAIT_FINISHED_ERR;
return 0;
}

void nodewait_start_connection(struct PICA_nodeaddr *a)
{
struct nodewait *nw;
#ifndef WIN32
pthread_t thr;
pthread_attr_t attr;
#else
DWORD thread_id;
HANDLE thread_h;
#endif

nw = nodewait_list_addnew();

if (!nw)
	return;

nw ->addr = *a;

PICA_debug1("starting connection to node %.255s %u ...", nw->addr.addr, nw->addr.port);

#ifndef WIN32
if (0 != pthread_attr_init(&attr))
	PICA_error("pthread_attr_init() call failed.");

pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

if (0 != pthread_create(&thr, &attr, nodewait_thread, (void*)nw))
	PICA_error("unable to create thread: %s", strerror(errno));

pthread_attr_destroy(&attr);
#else
thread_h = CreateThread(NULL, 4096, nodewait_thread, (void*)nw, 0, &thread_id);

if (NULL != thread_h)
	CloseHandle(thread_h);
else
	PICA_error("unable to create thread: %u", GetLastError());
#endif
}
