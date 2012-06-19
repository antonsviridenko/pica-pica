// Описание структур и функций для работы со списком адресов узлов 
// 
#ifndef PICA_NODEADDRLIST_H
#define PICA_NODEADDRLIST_H

#include <netinet/in.h>

struct PICA_nodeaddr_ipv4
{
int magick;
in_addr_t addr;
in_port_t port;//network byte order
};
struct PICA_nodeaddr_ipv6
{
int magick;
struct in6_addr addr;
in_port_t port;//network byte order
};
struct PICA_nodeaddr_dns
{
int magick;
unsigned int addr_len;
char addr[256];
in_port_t port;//network byte order
};

struct PICA_nodeaddr
{
char addr[256];//макс. длина DNS имени 255 символов
int inactive_count;//кол-во зафиксированных подряд случаев недоступности узла
time_t last_active;//
in_port_t port;//номер порта - host byte order

struct PICA_nodeaddr *next;
};

int PICA_nodeaddr_list_load(char* dbfilename,struct PICA_nodeaddr **list_head);
int PICA_nodeaddr_save(char* dbfilename, struct PICA_nodeaddr *naddr);
int PICA_nodeaddr_list_add();//
int PICA_nodeaddr_list_remove();//
void PICA_nodeaddr_list_free(struct PICA_nodeaddr *list_head);

#endif
