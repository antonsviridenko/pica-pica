/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef PICA_NETCONF_H
#define PICA_NETCONF_H

#ifndef WIN32

#include <netinet/in.h>
#include <arpa/inet.h>

#else

#define WIN32_LEAN_AND_MEAN 1
#define _WIN32_WINNT 0x501
#include <winsock2.h>
#include <ws2ipdef.h>
#include <wincrypt.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

typedef u_long in_addr_t;
typedef u_short in_port_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

int PICA_is_reserved_addr_ipv4(in_addr_t);

in_addr_t PICA_guess_listening_addr_ipv4();

#ifdef HAVE_LIBMINIUPNPC
int PICA_upnp_autoconfigure_ipv4(int public_port, int local_port, char *public_ip);
#endif

#ifdef __cplusplus
}
#endif

#endif

