#ifndef PICA_NODEWAIT_H
#define PICA_NODEWAIT_H

#include "PICA_nodeaddrlist.h"
#include "PICA_node.h"

#define PICA_NODEWAIT_NEW 1
#define PICA_NODEWAIT_RUNNING 2
#define PICA_NODEWAIT_FINISHED_OK 3
#define PICA_NODEWAIT_FINISHED_ERR 4

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
	
struct nodewait *next;
};

extern struct nodewait *nodewait_list;

struct nodewait *nodewait_list_addnew();
void nodewait_list_delete(struct nodewait *nw);
void nodewait_start_connection(struct PICA_nodeaddr *a);

#endif
