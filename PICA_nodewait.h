#ifndef PICA_NODEWAIT_H
#define PICA_NODEWAIT_H

#include "PICA_nodeaddrlist.h"
#include "PICA_node.h"

#define PICA_NODEWAIT_NEW 1
#define PICA_NODEWAIT_RESOLVING 5
#define PICA_NODEWAIT_RESOLVED 6
#define PICA_NODEWAIT_CONNECTING 7
#define PICA_NODEWAIT_CONNECTED 8
#define PICA_NODEWAIT_RESOLVING_FAILED 9
#define PICA_NODEWAIT_CONNECT_FAILED 10

struct nodewait
{
struct newconn nc;
int state;
time_t tmst;

//unsigned int addr_type;
// union address_ptr
// 	{
// 	struct PICA_nodeaddr_ipv4 *ipv4;
// 	struct PICA_nodeaddr_ipv6 *ipv6;
// 	struct PICA_nodeaddr_dns *dns;
// 	} addrptr;

struct PICA_nodeaddr addr;

struct addrinfo *ai;
int ai_errorcode;

struct nodewait *next;
};

extern struct nodewait *nodewait_list;

struct nodewait *nodewait_list_addnew();
void nodewait_list_delete(struct nodewait *nw);
void nodewait_start_resolve(struct PICA_nodeaddr *a);
void nodewait_start_connect(struct nodewait *nw);

#endif
