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
#include "PICA_netconf.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBMINIUPNPC
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#ifndef WIN32
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#define PREFIX4   htonl(0xf0000000U)
#define PREFIX8   htonl(0xff000000U)
#define PREFIX10 htonl(0xffc00000U)
#define PREFIX12 htonl(0xfff00000U)
#define PREFIX16 htonl(0xffff0000U)
#define PREFIX24 htonl(0xffffff00U)

#define MAKEIP(a, b, c, d) (htonl( (a&0xFF) << 24 | (b&0xFF) << 16 | (c&0xFF) << 8 | (d&0xFF) ))


int PICA_is_reserved_addr_ipv4(in_addr_t addr)
{
	if ((addr & PREFIX8)  == 0 //0.0.0.0/8
	        ||  (addr & PREFIX8)  == MAKEIP(10, 0, 0, 0) //10.0.0.0/8
	        ||  (addr & PREFIX10) == MAKEIP(100, 64, 0, 0) //100.64.0.0/10
	        ||  (addr & PREFIX8)  == MAKEIP(127, 0, 0, 0)//127.0.0.0/8
	        ||  (addr & PREFIX16) == MAKEIP(169, 254, 0, 0) //169.254.0.0/16
	        ||  (addr & PREFIX12) == MAKEIP(172, 16, 0, 0) //172.16.0.0/12
	        ||  (addr & PREFIX16) == MAKEIP(192, 168, 0, 0) //192.168.0.0/16
	        ||  (addr & PREFIX4)  == MAKEIP(224, 0, 0, 0) //224.0.0.0/4
	        ||  (addr & PREFIX4)  == MAKEIP(240, 0, 0, 0) //240.0.0.0/4
	        ||   addr == INADDR_BROADCAST
	   )
		return 1;

	return 0;
}

int PICA_is_private_addr_ipv4(in_addr_t addr)
{
	return (addr & PREFIX8)  == MAKEIP(10, 0, 0, 0)
	       || (addr & PREFIX12) == MAKEIP(172, 16, 0, 0)
	       || (addr & PREFIX16) == MAKEIP(192, 168, 0, 0)
	       || (addr & PREFIX10) == MAKEIP(100, 64, 0, 0);
}

/* Whether an interface address is worth telling a peer about at all: either
 * a private one, reachable from the same network, or a public one.
 */
static int is_usable_ifaddr_ipv4(in_addr_t addr)
{
	return PICA_is_private_addr_ipv4(addr) || !PICA_is_reserved_addr_ipv4(addr);
}

/* Collects the usable addresses into addrs, the private ones (want_private)
 * or the public ones, skipping the ones already there.
 */
static void ifaddr_add(in_addr_t *addrs, int maxcount, int *count, in_addr_t a, int want_private)
{
	int i;

	if (!is_usable_ifaddr_ipv4(a) || PICA_is_private_addr_ipv4(a) != want_private)
		return;

	for (i = 0; i < *count; i++)
		if (addrs[i] == a)
			return;

	if (*count < maxcount)
		addrs[(*count)++] = a;
}

#ifndef WIN32
int PICA_get_ifaddrs_ipv4(in_addr_t *addrs, int maxcount)
{
	struct ifaddrs *ifaddr, *ifa;
	int count = 0;
	int want_private;

	if (!addrs || maxcount <= 0)
		return 0;

	if (getifaddrs(&ifaddr) == -1)
		return 0;

	/* two passes: the private addresses go first, see the 0x0B message */
	for (want_private = 1; want_private >= 0; want_private--)
		for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
				continue;

			if (ifa->ifa_flags & IFF_LOOPBACK)
				continue;

			if (!((ifa->ifa_flags & IFF_RUNNING) && (ifa->ifa_flags & IFF_UP)))
				continue;

			ifaddr_add(addrs, maxcount, &count,
			           ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr, want_private);
		}

	freeifaddrs(ifaddr);

	return count;
}
#else
int PICA_get_ifaddrs_ipv4(in_addr_t *addrs, int maxcount)
{
	PIP_ADAPTER_ADDRESSES buf = NULL, ap;
	PIP_ADAPTER_UNICAST_ADDRESS ua;
	ULONG size = 16384;
	ULONG ret;
	int count = 0;
	int want_private;
	int tries;

	if (!addrs || maxcount <= 0)
		return 0;

	/* The size needed is only known by trying, and can grow between the
	 * attempts if an adapter appears meanwhile.
	 */
	for (tries = 0; tries < 3; tries++)
	{
		buf = (PIP_ADAPTER_ADDRESSES)malloc(size);

		if (!buf)
			return 0;

		ret = GetAdaptersAddresses(AF_INET,
		                           GAA_FLAG_SKIP_ANYCAST
		                           | GAA_FLAG_SKIP_MULTICAST
		                           | GAA_FLAG_SKIP_DNS_SERVER
		                           | GAA_FLAG_SKIP_FRIENDLY_NAME,
		                           NULL, buf, &size);

		if (ret == NO_ERROR)
			break;

		free(buf);
		buf = NULL;

		if (ret != ERROR_BUFFER_OVERFLOW)
			return 0;
	}

	if (!buf)
		return 0;

	for (want_private = 1; want_private >= 0; want_private--)
		for (ap = buf; ap; ap = ap->Next)
		{
			if (ap->OperStatus != IfOperStatusUp || ap->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
				continue;

			for (ua = ap->FirstUnicastAddress; ua; ua = ua->Next)
			{
				if (ua->Address.lpSockaddr->sa_family != AF_INET)
					continue;

				ifaddr_add(addrs, maxcount, &count,
				           ((struct sockaddr_in*)ua->Address.lpSockaddr)->sin_addr.s_addr, want_private);
			}
		}

	free(buf);

	return count;
}
#endif

in_addr_t PICA_guess_listening_addr_ipv4()
{
	in_addr_t addrs[PICA_IFADDRS_MAX];
	int n, i;

	n = PICA_get_ifaddrs_ipv4(addrs, PICA_IFADDRS_MAX);

	if (n == 0)
		return INADDR_ANY;

	/* the first public address if there is one, the first private one
	 * otherwise
	 */
	for (i = 0; i < n; i++)
		if (!PICA_is_private_addr_ipv4(addrs[i]))
			return addrs[i];

	return addrs[0];
}

#ifdef HAVE_LIBMINIUPNPC

/* UPnP error codes this code acts on, rather than just reporting */
#define UPNP_ERR_CONFLICT          718
#define UPNP_ERR_PERMONLY          725
#define UPNP_ERR_CONFLICTOTHERMECH 729

/* sizes the IGD's mapping table entries are read into, from
 * UPNP_GetSpecificPortMappingEntry()
 */
#define ENTRY_CLIENT_SIZE 16
#define ENTRY_PORT_SIZE    6
#define ENTRY_DESC_SIZE   80
#define ENTRY_FLAG_SIZE    4
#define ENTRY_LEASE_SIZE  16

struct PICA_upnp_ctx
{
	struct UPNPUrls urls;
	struct IGDdatas data;
	/* our address on the side the IGD is on - what mappings point at */
	char lanaddr[64];
	int discover_timeout_ms;

	/* the mapping held through this context, if any */
	int mapped;
	int port;
	/* lease asked for, 0 for a permanent mapping */
	unsigned int lease_asked;
	/* lease the IGD granted, which can be less */
	unsigned int lease;
	char desc[ENTRY_DESC_SIZE];
};

static void portstr(char *buf, size_t buflen, int port)
{
	snprintf(buf, buflen, "%d", port);
}

/* Whether the request reached the IGD at all. miniupnpc answers its own
 * problems - the control URL not responding, a reply that did not parse -
 * with negative values, and anything the device itself refused with the
 * positive UPnP error code it sent.
 */
static int comm_error(int ret)
{
	return ret < 0;
}

static int is_conflict(int ret)
{
	return ret == UPNP_ERR_CONFLICT || ret == UPNP_ERR_CONFLICTOTHERMECH;
}

static void drop_igd(struct PICA_upnp_ctx *ctx)
{
	if (ctx->urls.controlURL)
	{
		FreeUPNPUrls(&ctx->urls);
		memset(&ctx->urls, 0, sizeof(ctx->urls));
	}
}

static int find_igd(struct PICA_upnp_ctx *ctx)
{
	struct UPNPDev *devlist;
	int error = 0;
	int ret;
#if MINIUPNPC_API_VERSION >= 18
	char wanaddr[64];
#endif

	drop_igd(ctx);

#if MINIUPNPC_API_VERSION < 14
	devlist = upnpDiscover(ctx->discover_timeout_ms, 0, 0, 0, 0, &error);
#else
	devlist = upnpDiscover(ctx->discover_timeout_ms, 0, 0, 0, 0, 2, &error);
#endif

	if (!devlist)
		return PICA_UPNP_ERRNOIGD;

#if MINIUPNPC_API_VERSION < 18
	ret = UPNP_GetValidIGD(devlist, &ctx->urls, &ctx->data, ctx->lanaddr, sizeof(ctx->lanaddr));
#else
	ret = UPNP_GetValidIGD(devlist, &ctx->urls, &ctx->data, ctx->lanaddr, sizeof(ctx->lanaddr),
	                       wanaddr, sizeof(wanaddr));
#endif

	freeUPNPDevlist(devlist);

	if (ret == 1)
		return PICA_UPNP_OK;

	/* Every outcome but "nothing found" leaves the URLs allocated, usable or
	 * not.
	 */
	drop_igd(ctx);

	/* The meaning of the other values changed when the wanaddr parameter
	 * appeared: before that there were three of them, now there are four and
	 * an IGD whose own external address is private has one of its own.
	 */
#if MINIUPNPC_API_VERSION < 18
	if (ret == 2)
		return PICA_UPNP_ERRNOTCONN;
#else
	if (ret == 2)
		return PICA_UPNP_ERRPRIVADDR;

	if (ret == 3)
		return PICA_UPNP_ERRNOTCONN;
#endif

	return PICA_UPNP_ERRNOIGD;
}

/* The control URLs are gone when an earlier attempt to find the IGD again
 * failed - after it rebooted, say - and have to be looked for once more
 * before anything can be asked of it.
 */
static int ensure_igd(struct PICA_upnp_ctx *ctx)
{
	if (ctx->urls.controlURL)
		return PICA_UPNP_OK;

	return find_igd(ctx);
}

/* Reads the IGD's mapping table entry for this port.
 * \return PICA_UPNP_OK and whether the entry points at the same port of this
 * very machine - i.e. whether it is ours, made by this client now or by an
 * earlier run of it - through ours, and the remaining lease through lease;
 * otherwise the UPnP error (714 NoSuchEntryInArray when there is none)
 */
static int read_entry(struct PICA_upnp_ctx *ctx, const char *proto, int port, int *ours, unsigned int *lease)
{
	char portbuf[8];
	char client[ENTRY_CLIENT_SIZE] = "";
	char intport[ENTRY_PORT_SIZE] = "";
	char desc[ENTRY_DESC_SIZE] = "";
	char enabled[ENTRY_FLAG_SIZE] = "";
	char leasebuf[ENTRY_LEASE_SIZE] = "";
	int ret;

	portstr(portbuf, sizeof(portbuf), port);

	ret = UPNP_GetSpecificPortMappingEntry(ctx->urls.controlURL, ctx->data.first.servicetype,
	                                       portbuf, proto, NULL,
	                                       client, intport, desc, enabled, leasebuf);

	if (ret != UPNPCOMMAND_SUCCESS)
		return comm_error(ret) ? PICA_UPNP_ERRIGD : ret;

	if (ours)
		*ours = strcmp(client, ctx->lanaddr) == 0 && atoi(intport) == port;

	if (lease)
		*lease = (unsigned int)strtoul(leasebuf, NULL, 10);

	return PICA_UPNP_OK;
}

static int entry_is_ours(struct PICA_upnp_ctx *ctx, const char *proto, int port)
{
	int ours = 0;

	return read_entry(ctx, proto, port, &ours, NULL) == PICA_UPNP_OK && ours;
}

static void del_mapping(struct PICA_upnp_ctx *ctx, const char *proto, int port)
{
	char portbuf[8];

	if (!ctx->urls.controlURL)
		return;

	portstr(portbuf, sizeof(portbuf), port);

	UPNP_DeletePortMapping(ctx->urls.controlURL, ctx->data.first.servicetype,
	                       portbuf, proto, NULL);
}

/* One AddPortMapping, external and internal port the same, with the lease
 * this context settled on. A device that only supports permanent mappings
 * says so with 725 instead of mapping anything; the context remembers that
 * and stops asking for a lease.
 */
static int add_mapping(struct PICA_upnp_ctx *ctx, const char *proto, int port)
{
	char portbuf[8], leasebuf[16];
	int ret;

	portstr(portbuf, sizeof(portbuf), port);
	snprintf(leasebuf, sizeof(leasebuf), "%u", ctx->lease_asked);

	ret = UPNP_AddPortMapping(ctx->urls.controlURL, ctx->data.first.servicetype,
	                          portbuf, portbuf, ctx->lanaddr, ctx->desc, proto, NULL, leasebuf);

	if (ret == UPNP_ERR_PERMONLY && ctx->lease_asked != 0)
	{
		ctx->lease_asked = 0;
		snprintf(leasebuf, sizeof(leasebuf), "0");

		ret = UPNP_AddPortMapping(ctx->urls.controlURL, ctx->data.first.servicetype,
		                          portbuf, portbuf, ctx->lanaddr, ctx->desc, proto, NULL, leasebuf);
	}

	/* A conflict with a mapping that is ours anyway - this client having been
	 * killed before it could delete it, or a renewal on a device that does
	 * not let a mapping be added over itself - is no conflict. Taking it out
	 * and making it again is what gets it a fresh lease.
	 */
	if (is_conflict(ret) && entry_is_ours(ctx, proto, port))
	{
		del_mapping(ctx, proto, port);

		ret = UPNP_AddPortMapping(ctx->urls.controlURL, ctx->data.first.servicetype,
		                          portbuf, portbuf, ctx->lanaddr, ctx->desc, proto, NULL, leasebuf);
	}

	if (ret == UPNPCOMMAND_SUCCESS)
		return PICA_UPNP_OK;

	if (comm_error(ret))
		return PICA_UPNP_ERRIGD;

	return is_conflict(ret) ? PICA_UPNP_ERRCONFLICT : ret;
}

/* Maps the port number for both protocols, or for none: a pair where only
 * one half could be had is of no use and is given back. A failure of the
 * second half after the first one succeeded means that this port number is
 * not to be had, whatever the device answered, so it is reported as a
 * conflict - the next number may well do.
 */
static int map_pair(struct PICA_upnp_ctx *ctx, int port)
{
	int ret;
	unsigned int granted = 0;

	ret = ensure_igd(ctx);

	if (ret != PICA_UPNP_OK)
		return ret;

	ret = add_mapping(ctx, "TCP", port);

	if (ret != PICA_UPNP_OK)
		return ret;

	ret = add_mapping(ctx, "UDP", port);

	if (ret != PICA_UPNP_OK)
	{
		del_mapping(ctx, "TCP", port);
		return ret == PICA_UPNP_ERRIGD ? ret : PICA_UPNP_ERRCONFLICT;
	}

	/* A device is free to grant a shorter lease than the one asked for -
	 * IGD:2 ones clamp it to a week at most - and renewal has to be timed by
	 * what it actually granted. Devices that do not report it are assumed to
	 * have granted the request.
	 */
	ctx->lease = ctx->lease_asked;

	if (read_entry(ctx, "TCP", port, NULL, &granted) == PICA_UPNP_OK
	        && granted > 0 && (ctx->lease == 0 || granted < ctx->lease))
		ctx->lease = granted;

	ctx->port = port;
	ctx->mapped = 1;

	return PICA_UPNP_OK;
}

int PICA_upnp_discover(int timeout_ms, struct PICA_upnp_ctx **ctx)
{
	struct PICA_upnp_ctx *c;
	int ret;

	if (!ctx || timeout_ms <= 0)
		return PICA_UPNP_ERRINVARG;

	c = (struct PICA_upnp_ctx*)calloc(1, sizeof(struct PICA_upnp_ctx));

	if (!c)
		return PICA_UPNP_ERRNOMEM;

	c->discover_timeout_ms = timeout_ms;

	ret = find_igd(c);

	if (ret != PICA_UPNP_OK)
	{
		free(c);
		return ret;
	}

	*ctx = c;

	return PICA_UPNP_OK;
}

void PICA_upnp_free(struct PICA_upnp_ctx *ctx)
{
	if (!ctx)
		return;

	PICA_upnp_unmap(ctx);
	drop_igd(ctx);
	free(ctx);
}

int PICA_upnp_external_addr(struct PICA_upnp_ctx *ctx, char *addr, size_t addrlen)
{
	/* UPNP_GetExternalIPAddress() writes an address of up to 16 octets and
	 * nothing else, so it gets a buffer that is known to be big enough.
	 */
	char buf[64] = "";
	int ret;

	if (!ctx || !addr || addrlen < 16)
		return PICA_UPNP_ERRINVARG;

	ret = ensure_igd(ctx);

	if (ret != PICA_UPNP_OK)
		return ret;

	ret = UPNP_GetExternalIPAddress(ctx->urls.controlURL, ctx->data.first.servicetype, buf);

	if (ret != UPNPCOMMAND_SUCCESS)
		return comm_error(ret) ? PICA_UPNP_ERRIGD : ret;

	if (inet_addr(buf) == INADDR_NONE)
		return PICA_UPNP_ERRIGD;

	/* Behind a second NAT - the provider's, usually - the IGD's external
	 * address is private too, and a mapping on it leads nowhere from the
	 * Internet.
	 */
	if (PICA_is_reserved_addr_ipv4(inet_addr(buf)))
		return PICA_UPNP_ERRPRIVADDR;

	snprintf(addr, addrlen, "%s", buf);

	return PICA_UPNP_OK;
}

const char *PICA_upnp_lan_addr(const struct PICA_upnp_ctx *ctx)
{
	return ctx ? ctx->lanaddr : "";
}

int PICA_upnp_map(struct PICA_upnp_ctx *ctx, int port, unsigned int lease_secs, const char *desc)
{
	if (!ctx || port <= 0 || port > 65535)
		return PICA_UPNP_ERRINVARG;

	PICA_upnp_unmap(ctx);

	ctx->lease_asked = lease_secs;
	snprintf(ctx->desc, sizeof(ctx->desc), "%s", desc ? desc : "Pica Pica");

	return map_pair(ctx, port);
}

int PICA_upnp_refresh(struct PICA_upnp_ctx *ctx)
{
	int ret, ours = 0;
	int port;

	if (!ctx || !ctx->mapped)
		return PICA_UPNP_ERRINVARG;

	port = ctx->port;

	ret = ensure_igd(ctx);

	if (ret != PICA_UPNP_OK)
	{
		ctx->mapped = 0;
		return ret;
	}

	/* Either the mapping is there and ours, or it is gone (714), or the port
	 * now points somewhere else, or the IGD does not answer at all - which of
	 * them it is decides what to do, so it has to be read rather than guessed.
	 */
	ret = read_entry(ctx, "TCP", port, &ours, NULL);

	if (ret == PICA_UPNP_ERRIGD)
	{
		/* The control URLs are an address and a path the IGD handed out
		 * once; after it rebooted, or changed address, they lead nowhere
		 * and the device has to be looked for again.
		 */
		ret = find_igd(ctx);

		if (ret != PICA_UPNP_OK)
		{
			ctx->mapped = 0;
			return ret;
		}
	}
	else if (ret == PICA_UPNP_OK && !ours)
	{
		/* Something else behind this IGD took the port over - another
		 * instance, another machine, or a different program altogether.
		 * Many devices let that happen silently instead of reporting a
		 * conflict. The mapping is not ours to renew any more.
		 */
		fprintf(stderr, "UPnP: external port %i has been taken over by another host\n", port);
		ctx->mapped = 0;
		return PICA_UPNP_ERRCONFLICT;
	}

	/* Adding the mapping again is what renews its lease; it also puts it back
	 * when the IGD has dropped or forgotten it in the meantime.
	 */
	ctx->mapped = 0;

	return map_pair(ctx, port);
}

void PICA_upnp_unmap(struct PICA_upnp_ctx *ctx)
{
	if (!ctx || !ctx->mapped)
		return;

	del_mapping(ctx, "TCP", ctx->port);
	del_mapping(ctx, "UDP", ctx->port);

	ctx->mapped = 0;
	ctx->port = 0;
}

int PICA_upnp_mapped(const struct PICA_upnp_ctx *ctx)
{
	return ctx && ctx->mapped;
}

int PICA_upnp_port(const struct PICA_upnp_ctx *ctx)
{
	return (ctx && ctx->mapped) ? ctx->port : 0;
}

unsigned int PICA_upnp_lease(const struct PICA_upnp_ctx *ctx)
{
	return ctx ? ctx->lease : 0;
}

const char *PICA_upnp_strerror(int ret)
{
	const char *s;

	switch (ret)
	{
	case PICA_UPNP_OK:
		return "success";

	case PICA_UPNP_ERRNOIGD:
		return "no UPnP internet gateway device found on the local network";

	case PICA_UPNP_ERRNOTCONN:
		return "the UPnP gateway reports that it has no internet connection";

	case PICA_UPNP_ERRNOMEM:
		return "out of memory";

	case PICA_UPNP_ERRINVARG:
		return "invalid argument";

	case PICA_UPNP_ERRCONFLICT:
		return "the port is already forwarded to another host";

	case PICA_UPNP_ERRPRIVADDR:
		return "the UPnP gateway itself has a private address, it is behind another NAT";

	case PICA_UPNP_ERRIGD:
		return "the UPnP gateway did not answer or answered something unusable";

	case PICA_UPNP_ERRNOSUPPORT:
		return "this build has no UPnP support";

	default:
		break;
	}

	s = strupnperror(ret);

	return s ? s : "unknown UPnP error";
}

#else /* !HAVE_LIBMINIUPNPC */

int PICA_upnp_discover(int timeout_ms, struct PICA_upnp_ctx **ctx)
{
	(void)timeout_ms;
	(void)ctx;
	return PICA_UPNP_ERRNOSUPPORT;
}

void PICA_upnp_free(struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
}

int PICA_upnp_external_addr(struct PICA_upnp_ctx *ctx, char *addr, size_t addrlen)
{
	(void)ctx;
	(void)addr;
	(void)addrlen;
	return PICA_UPNP_ERRNOSUPPORT;
}

const char *PICA_upnp_lan_addr(const struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
	return "";
}

int PICA_upnp_map(struct PICA_upnp_ctx *ctx, int port, unsigned int lease_secs, const char *desc)
{
	(void)ctx;
	(void)port;
	(void)lease_secs;
	(void)desc;
	return PICA_UPNP_ERRNOSUPPORT;
}

int PICA_upnp_refresh(struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
	return PICA_UPNP_ERRNOSUPPORT;
}

void PICA_upnp_unmap(struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
}

int PICA_upnp_mapped(const struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
	return 0;
}

int PICA_upnp_port(const struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
	return 0;
}

unsigned int PICA_upnp_lease(const struct PICA_upnp_ctx *ctx)
{
	(void)ctx;
	return 0;
}

const char *PICA_upnp_strerror(int ret)
{
	(void)ret;
	return "this build has no UPnP support";
}

#endif
