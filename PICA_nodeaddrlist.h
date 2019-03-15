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
// Описание структур и функций для работы со списком адресов узлов
//
#ifndef PICA_NODEADDRLIST_H
#define PICA_NODEADDRLIST_H

#ifndef WIN32

#include <netinet/in.h>

#else

#define _WIN32_WINNT 0x501
#include <winsock2.h>
#include <ws2tcpip.h>

typedef u_long in_addr_t;
typedef u_short in_port_t;
#endif

#include <time.h>

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


int PICA_nodeaddr_list_load(const char* dbfilename, struct PICA_nodeaddr **list_head);
int PICA_nodeaddr_save(const char* dbfilename, struct PICA_nodeaddr *naddr);
int PICA_nodeaddr_update(const char* dbfilename, struct PICA_nodeaddr *naddr, int is_alive);
int PICA_nodeaddr_cleanold(const char* dbfilename, int treshold);
void PICA_nodeaddr_list_free(struct PICA_nodeaddr *list_head);

#endif
