/*
	(c) Copyright  2012 - 2026 Anton Sviridenko
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

#include <stddef.h>

#ifndef WIN32

#include <netinet/in.h>
#include <arpa/inet.h>

#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
/* PICA_client.c and PICA_media.c include this after the system headers */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x501
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <wincrypt.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

typedef u_long in_addr_t;
typedef u_short in_port_t;
#endif

/* Most interface addresses PICA_get_ifaddrs_ipv4() reports. */
#define PICA_IFADDRS_MAX 32

/* UPnP return values. Negative ones are detected here, zero is success, and
 * positive ones are UPnP error codes answered by the device, as listed in
 * miniupnpc's upnpcommands.h. PICA_upnp_strerror() turns any of them into
 * something printable.
 */
#define PICA_UPNP_OK            0
/** nothing answered the discovery, or what did is not an IGD */
#define PICA_UPNP_ERRNOIGD     -1
/** an IGD, but its WAN connection is down */
#define PICA_UPNP_ERRNOTCONN   -2
#define PICA_UPNP_ERRNOMEM     -3
#define PICA_UPNP_ERRINVARG    -4
/** the port number is taken on the IGD, for TCP or for UDP, by another host
 *  or another program - the next number may still be free */
#define PICA_UPNP_ERRCONFLICT  -5
/** the IGD's own external address is private - carrier NAT behind the NAT,
 *  nothing mapped there can be reached from the Internet */
#define PICA_UPNP_ERRPRIVADDR  -6
/** the request did not reach the IGD, or it answered something unusable */
#define PICA_UPNP_ERRIGD       -7
/** this build has no UPnP support */
#define PICA_UPNP_ERRNOSUPPORT -8

/* Lease to ask for. Short enough that a mapping left behind by a client that
 * died without deleting it disappears on its own; long enough that renewing
 * it, at half of whatever the device granted, is a rare event.
 */
#define PICA_UPNP_LEASE_DEFAULT 3600

/* How long PICA_upnp_discover() waits for devices to answer. */
#define PICA_UPNP_DISCOVER_TIMEOUT 2000

/* A port mapping on a UPnP IGD.
 *
 * The context holds the result of finding the IGD together with the mapping
 * held through it, so that discovery - a multicast search that has to wait
 * for devices to answer - is paid once, and the mapping can be renewed and
 * deleted later on with the same control URLs.
 *
 * A mapping always covers TCP and UDP with the same port number, and the
 * external port number is the same as the local one: the directc2c
 * connection is made over TCP and the media of a call over UDP to the same
 * announced port, see the 0x0B message in doc/protocol-specification.txt.
 *
 * A mapping is never permanent, whatever lease is asked for: IGD:2 devices
 * clamp a request for an infinite lease to a week, and any device loses its
 * mappings when it reboots. A mapping that is meant to stay up has to be
 * renewed - see PICA_upnp_refresh().
 *
 * Every call blocks for as long as the device takes to answer, discovery for
 * the whole timeout, so a context belongs on a thread that can afford to
 * wait - never on one carrying media. Nothing here is thread safe; one
 * context belongs to one thread.
 *
 * Without miniupnpc every function is still there and fails with
 * PICA_UPNP_ERRNOSUPPORT, so that callers need no conditional code.
 */
struct PICA_upnp_ctx;

#ifdef __cplusplus
extern "C" {
#endif

/** 0.0.0.0/8, loopback, link-local, multicast, the reserved 240.0.0.0/4,
 *  broadcast, and the private ranges of PICA_is_private_addr_ipv4() */
int PICA_is_reserved_addr_ipv4(in_addr_t);

/** 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16 and the shared address space
 *  100.64.0.0/10 - addresses only reachable from the same local network,
 *  VPN or provider network */
int PICA_is_private_addr_ipv4(in_addr_t);

/**
 * IPv4 addresses of the network interfaces that are up, other than loopback
 * and link-local ones: the private addresses first, then the public ones, in
 * the order the system lists the interfaces in.
 * \param addrs filled with the addresses, network byte order
 * \param maxcount size of addrs
 * \return number of addresses stored, 0 if there are none or the system
 * cannot be asked
 */
int PICA_get_ifaddrs_ipv4(in_addr_t *addrs, int maxcount);

/**
 * The private addresses (see PICA_is_private_addr_ipv4()) of the network
 * interfaces that are up, with their netmasks, i.e. the private networks
 * this host is on.
 * \param addrs, masks filled with the addresses and their netmasks, network
 * byte order
 * \param maxcount size of addrs and masks
 * \return number of addresses stored, 0 if there are none or the system
 * cannot be asked
 */
int PICA_get_private_ifaddrs_ipv4(in_addr_t *addrs, in_addr_t *masks, int maxcount);

/**
 * Looks for the interface address of PICA_get_private_ifaddrs_ipv4() whose
 * network addr is on.
 * \return its index, or -1 if addr is on none of them
 */
int PICA_find_subnet_ipv4(in_addr_t addr, const in_addr_t *addrs, const in_addr_t *masks, int count);

in_addr_t PICA_guess_listening_addr_ipv4();

/**
 * Looks for an IGD on the local network and, having found one, returns a
 * context to use it with.
 * \param timeout_ms how long to wait for devices to answer,
 * PICA_UPNP_DISCOVER_TIMEOUT is sensible
 * \param ctx filled with the new context on success, untouched otherwise
 */
int PICA_upnp_discover(int timeout_ms, struct PICA_upnp_ctx **ctx);

/** Deletes the mapping the context holds, if any, and frees it. */
void PICA_upnp_free(struct PICA_upnp_ctx *ctx);

/**
 * The IGD's address on the Internet side, as it reports it itself.
 * Fails with PICA_UPNP_ERRPRIVADDR if that address is a private one, since
 * announcing it would be of no use to anybody.
 * \param addr buffer for the address as a string, at least 16 octets
 */
int PICA_upnp_external_addr(struct PICA_upnp_ctx *ctx, char *addr, size_t addrlen);

/** Our own address on the side the IGD is on, i.e. the address mappings made
 *  through this context point at. */
const char *PICA_upnp_lan_addr(const struct PICA_upnp_ctx *ctx);

/**
 * Maps the external port number port, TCP and UDP both, to the same port
 * number of this machine. Either both protocols are mapped or neither is.
 *
 * A mapping left behind by an earlier run of this same client (same machine,
 * same port) is recognised as ours and replaced rather than treated as a
 * conflict. A context holds one mapping; mapping again replaces it.
 *
 * \param lease_secs lease to ask for, 0 for a permanent mapping
 * \param desc description for the IGD's mapping table, may be NULL
 * \retval PICA_UPNP_ERRCONFLICT the port number is taken by somebody else,
 * for one of the protocols at least; the next number may be free
 */
int PICA_upnp_map(struct PICA_upnp_ctx *ctx, int port, unsigned int lease_secs, const char *desc);

/**
 * Renews the lease of the mapping this context holds, also putting it back
 * if the IGD dropped it in the meantime, and finding the IGD again if its
 * control URLs stopped working (it rebooted or changed address).
 *
 * \retval PICA_UPNP_ERRCONFLICT the external port has been taken over by
 * another host or program; the context no longer holds a mapping
 */
int PICA_upnp_refresh(struct PICA_upnp_ctx *ctx);

/** Deletes the mapping the context holds. The context stays usable. */
void PICA_upnp_unmap(struct PICA_upnp_ctx *ctx);

/** Whether the context currently holds a mapping. */
int PICA_upnp_mapped(const struct PICA_upnp_ctx *ctx);

/** Port number of the mapping the context holds, 0 if it holds none. */
int PICA_upnp_port(const struct PICA_upnp_ctx *ctx);

/**
 * Lease the IGD granted for the mapping, in seconds; 0 when it only supports
 * permanent mappings. A device is free to grant less than was asked for, so
 * this is what renewal has to be timed by.
 */
unsigned int PICA_upnp_lease(const struct PICA_upnp_ctx *ctx);

/** Printable form of any of the UPnP return values. Never NULL. */
const char *PICA_upnp_strerror(int ret);

#ifdef __cplusplus
}
#endif

#endif
