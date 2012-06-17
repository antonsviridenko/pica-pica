 
#ifndef PICA_NODEJOINSKYNET_H
#define PICA_NODEJOINSKYNET_H

int PICA_node_joinskynet(char* addrlistfilename,const char *my_addr);
int try_connect_to_node(char *addr, unsigned int port,struct newconn *nc);
int try_get_reply(struct newconn *nc);
void send_newnode(struct nodelink *nlp, const char *my_addr);

#ifdef JOINSKYNET //include from PICA_nodejoinskynet.c, private data types definition
#include "PICA_nodeaddrlist.h"


#endif


#endif
