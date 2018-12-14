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

#include "PICA_nodewait.h"
#include "PICA_log.h"
#include "PICA_common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
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
			if (nw->ai)
				freeaddrinfo(nw->ai);

			free(nw);

			return;
		}

		pp = &p->next;
		p = p->next;
	}
}

#ifdef WIN32
static DWORD WINAPI nodewait_resolve_thread (void *arg)
#else
static void  *nodewait_resolve_thread (void *arg)
#endif
{
	/*
	* DO NOT CALL LOGGING FUNCTIONS HERE, THEY ARE NOT THREAD-SAFE
	*/
	struct nodewait *nw = (struct nodewait *)arg;
	struct addrinfo h;
	char portbuf[8];


	memset(&h, 0, sizeof(struct addrinfo));

#ifdef AI_ADDRCONFIG
#ifdef AI_IDN
#define H_AI_FLAGS (AI_ADDRCONFIG | AI_IDN)
#else
#define H_AI_FLAGS AI_ADDRCONFIG
#endif
#else
#define H_AI_FLAGS 0
#endif

	h.ai_flags = H_AI_FLAGS;
	h.ai_family = AF_INET; //CONF - IPv4, IPv6, IPv4 then IPv6, IPv6 then IPv4
	h.ai_socktype = SOCK_STREAM;
	h.ai_protocol = IPPROTO_TCP;

	sprintf(portbuf, "%u", nw->addr.port);

	nw->ai_errorcode = getaddrinfo(nw->addr.addr, portbuf, &h, &nw->ai);

	if (nw->ai_errorcode)
		nw->state = PICA_NODEWAIT_RESOLVING_FAILED;
	else
		nw->state = PICA_NODEWAIT_RESOLVED;

	return 0;
}


#ifdef WIN32
static DWORD WINAPI nodewait_connect_thread (void *arg)
#else
static void  *nodewait_connect_thread (void *arg)
#endif
{
	/*
	* DO NOT CALL LOGGING FUNCTIONS HERE, THEY ARE NOT THREAD-SAFE
	*/
	struct nodewait *nw = (struct nodewait *)arg;
	struct  addrinfo *ap;


	ap = nw->ai;

	while(ap)
	{
		nw->nc.sck = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
		if 	(nw->nc.sck == SOCKET_ERROR)
		{
			ap = ap->ai_next;
			continue;
		}

		if (0 != connect(nw->nc.sck, ap->ai_addr, ap->ai_addrlen))
		{
			ap = ap->ai_next;

			CLOSE(nw->nc.sck);
			continue;
		}
		else
		{
			nw->nc.anonssl = SSL_new(anon_ctx);

			if (!nw->nc.anonssl)
			{
				CLOSE(nw->nc.sck);
				return (void  *)0;
			}

			if (!SSL_set_fd(nw->nc.anonssl, nw->nc.sck) || 1 != SSL_connect(nw->nc.anonssl))
			{
				SSL_free(nw->nc.anonssl);
				CLOSE(nw->nc.sck);
				ap = ap->ai_next;
				continue;
			}

			break;
		}
	}

	if (ap)
	{
		nw->nc.addr = *((struct sockaddr_in*)(ap->ai_addr));

		nw->state = PICA_NODEWAIT_CONNECTED;
		return (void  *)1;
	}

	nw->state = PICA_NODEWAIT_CONNECT_FAILED;
	return (void  *)0;
}

void nodewait_start_resolve(struct PICA_nodeaddr *a)
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

	PICA_debug1("starting to resolve  node address %.255s %u ...", nw->addr.addr, nw->addr.port);

	nw->state = PICA_NODEWAIT_RESOLVING;

#ifndef WIN32
	if (0 != pthread_attr_init(&attr))
		PICA_error("pthread_attr_init() call failed.");

	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (0 != pthread_create(&thr, &attr, nodewait_resolve_thread, (void*)nw))
		PICA_error("unable to create thread: %s", strerror(errno));

	pthread_attr_destroy(&attr);
#else
	thread_h = CreateThread(NULL, 4096, nodewait_resolve_thread, (void*)nw, 0, &thread_id);

	if (NULL != thread_h)
		CloseHandle(thread_h);
	else
		PICA_error("unable to create thread: %u", GetLastError());
#endif
}

void nodewait_start_connect(struct nodewait *nw)
{
#ifndef WIN32
	pthread_t thr;
	pthread_attr_t attr;
#else
	DWORD thread_id;
	HANDLE thread_h;
#endif

	PICA_debug1("connecting to  node  %.255s %u ...", nw->addr.addr, nw->addr.port);

	nw->state = PICA_NODEWAIT_CONNECTING;
#ifndef WIN32
	if (0 != pthread_attr_init(&attr))
		PICA_error("pthread_attr_init() call failed.");

	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (0 != pthread_create(&thr, &attr, nodewait_connect_thread, (void*)nw))
		PICA_error("unable to create thread: %s", strerror(errno));

	pthread_attr_destroy(&attr);
#else
	thread_h = CreateThread(NULL, 4096, nodewait_connect_thread, (void*)nw, 0, &thread_id);

	if (NULL != thread_h)
		CloseHandle(thread_h);
	else
		PICA_error("unable to create thread: %u", GetLastError());
#endif

}
