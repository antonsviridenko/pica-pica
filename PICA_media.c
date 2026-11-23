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
#include "PICA_security.h"
#include "PICA_client.h"
#include "PICA_media.h"
#include "PICA_proto.h"
#include "PICA_common.h"
#include "PICA_netconf.h"
#include <openssl/err.h>
#include <openssl/rand.h>
#include <string.h>
#include <time.h>
#ifndef WIN32
#include <errno.h>
#endif

#define PICA_DEBUG

#ifdef PICA_DEBUG
#define PICA_TRACEFUNC fprintf(stderr, "%s()\n", __PRETTY_FUNCTION__);
#else
#define PICA_TRACEFUNC
#endif

static void media_fail(struct PICA_media_channel *m);
static void media_handshake(struct PICA_media_channel *m);
static int media_punch_start(struct PICA_c2c *c2c);

#ifdef WIN32
uint64_t PICA_media_now(void)
{
	/* GetTickCount64() would be simpler, but needs Vista as the minimum
	 * target and the headers are set up for XP
	 */
	static LARGE_INTEGER freq;
	LARGE_INTEGER count;

	if (freq.QuadPart == 0)
		QueryPerformanceFrequency(&freq);

	QueryPerformanceCounter(&count);

	return (uint64_t)(count.QuadPart / (freq.QuadPart / 1000));
}
#else
uint64_t PICA_media_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
#endif

static int media_socket_open(SOCKET *s, int local_port)
{
	struct sockaddr_in a;

	*s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (*s == SOCKET_ERROR)
		return PICA_ERRSOCK;

	/* No SO_REUSEADDR: UDP has no TIME_WAIT for it to get around, while for
	 * UDP it lets a second socket bind to a port another one already uses -
	 * and the listener has to find out when its port is taken, so that it
	 * can try the next one instead of sharing it.
	 */
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = INADDR_ANY;
	/* local_port 0 asks the system for an unused port, which is what the
	 * connecting side wants - only the accepting side needs a port number
	 * known to the peer in advance.
	 */
	a.sin_port = htons(local_port);

	if (bind(*s, (const struct sockaddr*)&a, sizeof(a)) == SOCKET_ERROR)
	{
		CLOSE(*s);
		*s = -1;
		return PICA_ERRSOCK;
	}

	IOCTLSETNONBLOCKINGSOCKET(*s, 1);

	return PICA_OK;
}

int PICA_media_listener_new(const struct PICA_acc *acc, int public_port, int local_port,
                            struct PICA_media_listener **ml)
{
	struct PICA_media_listener *l;
	int ret;

	PICA_TRACEFUNC

	if (!acc || !ml || public_port <= 0 || public_port > 65535 || local_port <= 0 || local_port > 65535)
		return PICA_ERRINVARG;

	l = *ml = (struct PICA_media_listener*)calloc(sizeof(struct PICA_media_listener), 1);

	if (!l)
		return PICA_ERRNOMEM;

	l->acc = acc;
	l->public_port = public_port;
	l->local_port = local_port;

	ret = media_socket_open(&l->sck, local_port);

	if (ret != PICA_OK)
	{
		free(l);
		*ml = 0;
		return ret;
	}

	return PICA_OK;
}

void PICA_media_listener_close(struct PICA_media_listener *ml)
{
	PICA_TRACEFUNC

	if (!ml)
		return;

	/* A call using the socket keeps it until it ends, as if it had opened
	 * it itself: closing the listener stops new connections, not this one.
	 */
	if (ml->channel)
	{
		ml->channel->listener = NULL;
		ml->channel->owns_sck = 1;
		ml->channel = NULL;
		ml->sck = -1;
	}

	if (ml->sck >= 0)
		CLOSE(ml->sck);

	free(ml);
}

/* The listener's socket gets connect()ed to the peer for the duration of a
 * call, so that the system drops everything coming from anywhere else. It has
 * to be given back in its original, unconnected state afterwards; recreating
 * it is the one way of doing that which behaves the same everywhere.
 */
static void media_listener_rearm(struct PICA_media_listener *ml)
{
	PICA_TRACEFUNC

	if (ml->sck >= 0)
		CLOSE(ml->sck);

	if (media_socket_open(&ml->sck, ml->local_port) != PICA_OK)
	{
		fprintf(stderr, "failed to reopen the media listening socket on port %i\n", ml->local_port);
		ml->sck = -1;
	}
}

static int media_ssl_new(struct PICA_media_channel *m)
{
	BIO *bio;

	m->ssl = SSL_new(m->c2c->acc->dtls_ctx);

	if (!m->ssl)
		return PICA_ERRSSL;

	bio = BIO_new_dgram(m->sck, BIO_NOCLOSE);

	if (!bio)
	{
		SSL_free(m->ssl);
		m->ssl = NULL;
		return PICA_ERRSSL;
	}

	/* The socket is connected, so the system already knows where the
	 * datagrams go; the BIO has to be told as well, otherwise it has no
	 * peer address to hand to sendto().
	 */
	BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, (char*)&m->peer_addr);

	SSL_set_bio(m->ssl, bio, bio);

	SSL_set_verify(m->ssl, SSL_VERIFY_PEER, PICA_verify_callback);
	SSL_set_verify_depth(m->ssl, 1);

	/* OpenSSL performs no path MTU discovery of its own: off Linux it cannot
	 * even read the value the system discovered, and falls back to a 228
	 * octet floor that would fragment every handshake message. Telling it
	 * the MTU explicitly, and stopping it from asking the BIO
	 * (SSL_OP_NO_QUERY_MTU), gives the same behaviour everywhere. It also
	 * keeps OpenSSL from shrinking the value on its own after two handshake
	 * retransmissions, which it never undoes.
	 * This has to happen before the handshake starts: with
	 * SSL_OP_NO_QUERY_MTU set and no MTU, writing a handshake message fails.
	 */
	SSL_set_options(m->ssl, SSL_OP_NO_QUERY_MTU);
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	DTLS_set_link_mtu(m->ssl, PICA_MEDIA_LINK_MTU);
#else
	SSL_set_mtu(m->ssl, PICA_MEDIA_LINK_MTU - PICA_MEDIA_IPUDP_OVERHEAD);
#endif

	return PICA_OK;
}

static struct PICA_media_channel *media_channel_new(struct PICA_c2c *c2c)
{
	struct PICA_media_channel *m;

	m = (struct PICA_media_channel*)calloc(sizeof(struct PICA_media_channel), 1);

	if (!m)
		return NULL;

	m->c2c = c2c;
	m->sck = -1;
	m->started = time(0);
	m->last_tx = m->started;

	return m;
}

/* "address:port", for the log; buf has PICA_MEDIA_ADDRSTRSIZE octets */
#define PICA_MEDIA_ADDRSTRSIZE (INET6_ADDRSTRLEN + 6)

static const char *media_addrstr(const struct sockaddr_in *a, char *buf)
{
	const unsigned char *ip = (const unsigned char*)&a->sin_addr.s_addr;

	sprintf(buf, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], (unsigned int)ntohs(a->sin_port));

	return buf;
}

/* Whether an address from the peer's 0x70/0x71 is one worth sending probes
 * to: a unicast address of a host, and a port. A private one only if it is
 * on one of the private networks this host is on, given by ifaddrs/ifmasks
 * - anywhere else it belongs to some other network, as for 0x0B. Logs the
 * decision.
 */
static int is_probe_target(const struct sockaddr_in *a, const in_addr_t *ifaddrs,
                           const in_addr_t *ifmasks, int ifcount)
{
	in_addr_t addr = a->sin_addr.s_addr;
	char addrstr[PICA_MEDIA_ADDRSTRSIZE];
	int i;

	media_addrstr(a, addrstr);

	if (a->sin_port == 0)
	{
		fprintf(stderr, "media: call address %s - ignored: no port number\n", addrstr);
		return 0;
	}

	if (!PICA_is_private_addr_ipv4(addr))
	{
		if (PICA_is_reserved_addr_ipv4(addr))
		{
			fprintf(stderr, "media: call address %s - ignored: reserved address\n", addrstr);
			return 0;
		}

		fprintf(stderr, "media: call address %s - public, will be probed for %u ms\n", addrstr,
		        PICA_MEDIA_PUNCH_TIMEOUT);
		return 1;
	}

	if (ifcount == 0)
	{
		fprintf(stderr, "media: call address %s - private, ignored: this host is on no private network\n", addrstr);
		return 0;
	}

	i = PICA_find_subnet_ipv4(addr, ifaddrs, ifmasks, ifcount);

	if (i < 0)
	{
		fprintf(stderr, "media: call address %s - private, ignored: not on a private network of this host\n",
		        addrstr);
		return 0;
	}

	{
		const unsigned char *ip = (const unsigned char*)&ifaddrs[i];
		const unsigned char *mask = (const unsigned char*)&ifmasks[i];

		fprintf(stderr, "media: call address %s - private, on the network of local address %u.%u.%u.%u "
		        "netmask %u.%u.%u.%u, will be probed for %u ms\n", addrstr,
		        ip[0], ip[1], ip[2], ip[3], mask[0], mask[1], mask[2], mask[3], PICA_MEDIA_LAN_PROBE_TIMEOUT);
	}

	return 1;
}

/* Stops a request for the call socket's address still waiting for the node,
 * so that a late answer is not delivered to a call that no longer wants it.
 */
static void media_call_stop_learning(struct PICA_c2c *c2c)
{
	struct PICA_media_call *mc = c2c->media_call;

	if (!mc->learning)
		return;

	mc->learning = 0;

	if (c2c->conn->udpaddr_c2c == c2c)
	{
		c2c->conn->udpaddr_c2c = NULL;
		c2c->conn->udpaddr_state = PICA_UDPADDR_STATE_IDLE;
	}
}

int PICA_media_call_prepare(struct PICA_c2c *c2c, int is_caller,
                            const unsigned char *peer_info, unsigned int peer_info_len)
{
	struct PICA_media_call *mc;
	struct sockaddr_in a;
#ifdef WIN32
	int alen = sizeof(a);
#else
	socklen_t alen = sizeof(a);
#endif

	PICA_TRACEFUNC

	PICA_media_call_free(c2c);

	mc = (struct PICA_media_call*)calloc(sizeof(struct PICA_media_call), 1);

	if (!mc)
		return PICA_ERRNOMEM;

	mc->sck = -1;
	mc->is_caller = is_caller;

	if (RAND_bytes(mc->token, sizeof(mc->token)) != 1)
	{
		free(mc);
		return PICA_ERRSSL;
	}

	c2c->media_call = mc;

	if (peer_info)
		PICA_media_call_read_info(c2c, peer_info, peer_info_len);

	/* Hole punching reveals this client's addresses to the peer, which the
	 * user has not agreed to with direct connections disabled. A callee
	 * whose caller sent no addresses could not punch anywhere either.
	 */
	if (c2c->conn->directc2c_config == PICA_DIRECTC2C_CFG_DISABLED || !c2c->acc->dtls_ctx)
		return PICA_OK;

	if (!is_caller && mc->peer_addrs_count == 0)
		return PICA_OK;

	if (media_socket_open(&mc->sck, 0) != PICA_OK)
		return PICA_OK;

	if (getsockname(mc->sck, (struct sockaddr*)&a, &alen) == SOCKET_ERROR)
	{
		CLOSE(mc->sck);
		mc->sck = -1;
		return PICA_OK;
	}

	mc->local_port = ntohs(a.sin_port);

	/* What the node sees is what the NAT in front of us maps this socket to
	 * - the one address a peer outside the local network can punch through
	 * to. Without it, only the interface addresses are announced.
	 */
	if (PICA_request_udp_address(c2c->conn, mc->sck) == PICA_OK)
	{
		c2c->conn->udpaddr_c2c = c2c;
		mc->learning = 1;
		mc->learn_started = PICA_media_now();
	}

	return PICA_OK;
}

void PICA_media_call_learned(struct PICA_c2c *c2c, int status, const unsigned char *addr_bin, int type, uint16_t port)
{
	struct PICA_media_call *mc = c2c->media_call;

	PICA_TRACEFUNC

	if (!mc || !mc->learning)
		return;

	mc->learning = 0;

	if (status != PICA_OK || type != PICA_PROTO_MYUDPADDR_IPV4)
		return;

	memset(&mc->learned_addr, 0, sizeof(mc->learned_addr));
	mc->learned_addr.sin_family = AF_INET;
	memcpy(&mc->learned_addr.sin_addr.s_addr, addr_bin, 4);
	mc->learned_addr.sin_port = htons(port);
	mc->learned = 1;
}

int PICA_media_call_ready(struct PICA_c2c *c2c)
{
	struct PICA_media_call *mc = c2c->media_call;

	if (!mc || !mc->learning)
		return 1;

	return PICA_media_now() >= mc->pending_deadline;
}

static unsigned char *write_addr_ipv4(unsigned char *p, in_addr_t addr, uint16_t port_netorder)
{
	*p++ = PICA_PROTO_DIRECTC2C_IPV4;
	memcpy(p, &addr, 4);
	p += 4;
	memcpy(p, &port_netorder, 2);

	return p + 2;
}

unsigned int PICA_media_call_write_info(struct PICA_c2c *c2c, unsigned char *buf)
{
	struct PICA_media_call *mc = c2c->media_call;
	unsigned char *p = buf;
	in_addr_t ifaddrs[PICA_IFADDRS_MAX];
	int n, i, count = 0;
	int learned_is_ifaddr = 0;

	PICA_TRACEFUNC

	if (!mc)
		return 0;

	/* whatever the node did not answer in time stays unknown */
	media_call_stop_learning(c2c);

	memcpy(p, mc->token, PICA_PROTO_CALL_TOKENSIZE);
	p += PICA_PROTO_CALL_TOKENSIZE;

	if (mc->sck < 0)
		return (unsigned int)(p - buf);

	/* The interface addresses first, so that two clients on the same local
	 * network reach each other inside it, which works whether or not their
	 * router can loop traffic for its own external address back in. One
	 * place is kept for the address the node reported.
	 */
	n = PICA_get_ifaddrs_ipv4(ifaddrs, PICA_IFADDRS_MAX);

	for (i = 0; i < n && count < PICA_PROTO_CALL_MAXADDRS - 1; i++)
	{
		if (mc->learned && ifaddrs[i] == mc->learned_addr.sin_addr.s_addr
		        && ntohs(mc->learned_addr.sin_port) == mc->local_port)
			learned_is_ifaddr = 1;

		p = write_addr_ipv4(p, ifaddrs[i], htons((uint16_t)mc->local_port));
		count++;
	}

	/* the same as an interface address when there is no NAT on the way */
	if (mc->learned && !learned_is_ifaddr)
		p = write_addr_ipv4(p, mc->learned_addr.sin_addr.s_addr, mc->learned_addr.sin_port);

	return (unsigned int)(p - buf);
}

int PICA_media_call_read_info(struct PICA_c2c *c2c, const unsigned char *buf, unsigned int len)
{
	struct PICA_media_call *mc = c2c->media_call;
	in_addr_t ifaddrs[PICA_IFADDRS_MAX], ifmasks[PICA_IFADDRS_MAX];
	int ifcount;
	unsigned int pos;
	int addresses = 0;

	if (!mc)
		return 0;

	mc->peer_info_received = 1;
	mc->peer_addrs_count = 0;

	if (len < PICA_PROTO_CALL_TOKENSIZE)
		return 0;

	memcpy(mc->peer_token, buf, PICA_PROTO_CALL_TOKENSIZE);
	pos = PICA_PROTO_CALL_TOKENSIZE;

	ifcount = PICA_get_private_ifaddrs_ipv4(ifaddrs, ifmasks, PICA_IFADDRS_MAX);

	while (pos < len && addresses < PICA_PROTO_CALL_MAXADDRS)
	{
		struct sockaddr_in *a;

		switch (buf[pos])
		{
		case PICA_PROTO_DIRECTC2C_IPV4:
			if (pos + PICA_PROTO_DIRECTC2C_ADDRLIST_ITEM_IPV4_SIZE > len)
				goto malformed;

			addresses++;
			a = &mc->peer_addrs[mc->peer_addrs_count];
			memset(a, 0, sizeof(*a));
			a->sin_family = AF_INET;
			memcpy(&a->sin_addr.s_addr, buf + pos + 1, 4);
			memcpy(&a->sin_port, buf + pos + 5, 2);

			if (is_probe_target(a, ifaddrs, ifmasks, ifcount))
				mc->peer_addrs_count++;

			pos += PICA_PROTO_DIRECTC2C_ADDRLIST_ITEM_IPV4_SIZE;
			break;

		case PICA_PROTO_DIRECTC2C_IPV6:
			/* counts as an address, but the media sockets are IPv4 ones */
			if (pos + PICA_PROTO_DIRECTC2C_ADDRLIST_ITEM_IPV6_SIZE > len)
				goto malformed;

			addresses++;
			pos += PICA_PROTO_DIRECTC2C_ADDRLIST_ITEM_IPV6_SIZE;
			break;

		case PICA_PROTO_DIRECTC2C_DNS:
			if (pos + 1 >= len || pos + 2 + buf[pos + 1] + 2 > len)
				goto malformed;

			pos += 2 + buf[pos + 1] + 2;
			break;

		default:
			/* no way of knowing where an element of an unknown type ends */
			goto malformed;
		}
	}

	return 1;

malformed:
	/* An address list that does not parse cannot be relied on in any of its
	 * parts. The call goes ahead, only without hole punching.
	 */
	mc->peer_addrs_count = 0;
	return 0;
}

void PICA_media_call_free(struct PICA_c2c *c2c)
{
	struct PICA_media_call *mc = c2c->media_call;

	if (!mc)
		return;

	/* the media connection of attempt 2 uses the call socket */
	if (c2c->media && c2c->media->attempt == 2)
		PICA_media_close(c2c);

	media_call_stop_learning(c2c);

	if (mc->sck >= 0)
		CLOSE(mc->sck);

	c2c->media_call = NULL;
	free(mc);
}

/* Attempt 1, over the address of the directc2c connection. It goes first
 * because that address is known to work already - the port behind it is
 * public or forwarded, whatever the NATs on the way do with UDP.
 */
static int media_direct_start(struct PICA_c2c *c2c)
{
	struct PICA_media_channel *m;
	int ret;

	PICA_TRACEFUNC

	if (c2c->directc2c_state != PICA_DIRECTC2C_STATE_ACTIVE || !c2c->direct)
		return PICA_ERRNOTFOUND;

	m = media_channel_new(c2c);

	if (!m)
		return PICA_ERRNOMEM;

	m->attempt = 1;
	m->deadline = PICA_media_now() + PICA_MEDIA_DIRECT_TIMEOUT;

	if (c2c->direct->is_outgoing == PICA_DIRECTC2C_OUTGOING)
	{
		/* We reached the peer over TCP at this address, so we are the one
		 * that can reach it over UDP as well - the peer's announced port
		 * number is used for both.
		 */
		m->is_dtls_client = 1;
		m->peer_addr = c2c->direct->addr;

		ret = media_socket_open(&m->sck, 0);

		if (ret != PICA_OK)
		{
			free(m);
			return ret;
		}

		m->owns_sck = 1;

		if (connect(m->sck, (struct sockaddr*)&m->peer_addr, sizeof(m->peer_addr)) == SOCKET_ERROR)
		{
			CLOSE(m->sck);
			free(m);
			return PICA_ERRSOCK;
		}

		if (media_ssl_new(m) != PICA_OK)
		{
			CLOSE(m->sck);
			free(m);
			return PICA_ERRSSL;
		}

		SSL_set_connect_state(m->ssl);

		m->state = PICA_MEDIA_STATE_HANDSHAKE;
		c2c->media = m;

		/* can fail, and start attempt 2 from there */
		media_handshake(m);
	}
	else
	{
		struct PICA_media_listener *ml;

		if (!c2c->conn->directc2c_listener || !c2c->conn->directc2c_listener->media)
		{
			free(m);
			return PICA_ERRNOTFOUND;
		}

		ml = c2c->conn->directc2c_listener->media;

		if (ml->sck < 0 || ml->channel)
		{
			free(m);
			return PICA_ERRSOCK;
		}

		/* Nothing to do until the peer sends its first datagram: the source
		 * address it arrives from is the only way to learn which port its
		 * NAT gave it.
		 */
		m->listener = ml;
		m->sck = ml->sck;
		m->owns_sck = 0;
		m->state = PICA_MEDIA_STATE_LISTENING;

		ml->channel = m;
		c2c->media = m;
	}

	return PICA_OK;
}

static void media_send_probe_to(struct PICA_media_channel *m, const struct sockaddr_in *to)
{
	unsigned char probe[PICA_PROTO_MEDIAPROBE_SIZE];

	probe[0] = probe[1] = PICA_PROTO_MEDIAPROBE;
	memcpy(probe + 2, m->c2c->media_call->peer_token, PICA_PROTO_CALL_TOKENSIZE);

	/* A failed send is the same as a datagram lost on the way. Once an
	 * address is selected the socket is connected to it, and sendto() with
	 * an address is not allowed on a connected socket everywhere.
	 */
	if (to)
		sendto(m->sck, (const char*)probe, sizeof(probe), 0, (const struct sockaddr*)to, sizeof(*to));
	else
		send(m->sck, (const char*)probe, sizeof(probe), 0);
}

/* Sends the probes that are due: to every address of the peer until one is
 * selected, to the selected one afterwards until the peer's first DTLS
 * datagram shows that it has selected us too.
 */
static void media_send_probes(struct PICA_media_channel *m, uint64_t now)
{
	struct PICA_media_call *mc = m->c2c->media_call;
	int lan_due, public_due;
	int i;

	if (m->dtls_seen)
		return;

	public_due = now - m->last_probe >= PICA_MEDIA_PROBE_INTERVAL;

	if (m->state != PICA_MEDIA_STATE_PUNCHING)
	{
		if (public_due)
		{
			m->last_probe = now;
			media_send_probe_to(m, NULL);
		}

		return;
	}

	/* the private addresses only at first, see PICA_MEDIA_LAN_PROBE_TIMEOUT */
	lan_due = now - m->punch_started <= PICA_MEDIA_LAN_PROBE_TIMEOUT
	          && now - m->last_lan_probe >= PICA_MEDIA_LAN_PROBE_INTERVAL;

	if (lan_due)
		m->last_lan_probe = now;

	if (public_due)
		m->last_probe = now;

	for (i = 0; i < mc->peer_addrs_count; i++)
		if (PICA_is_private_addr_ipv4(mc->peer_addrs[i].sin_addr.s_addr) ? lan_due : public_due)
			media_send_probe_to(m, &mc->peer_addrs[i]);
}

/* Attempt 2, by hole punching between the call sockets. */
static int media_punch_start(struct PICA_c2c *c2c)
{
	struct PICA_media_call *mc = c2c->media_call;
	struct PICA_media_channel *m;

	PICA_TRACEFUNC

	if (c2c->media || !mc || mc->sck < 0 || mc->peer_addrs_count == 0)
		return PICA_ERRNOTFOUND;

	m = media_channel_new(c2c);

	if (!m)
		return PICA_ERRNOMEM;

	m->attempt = 2;
	m->sck = mc->sck;
	m->owns_sck = 0;
	m->is_dtls_client = mc->is_caller;
	m->state = PICA_MEDIA_STATE_PUNCHING;
	m->punch_started = PICA_media_now();
	m->deadline = m->punch_started + PICA_MEDIA_PUNCH_TIMEOUT;

	c2c->media = m;

	fprintf(stderr, "media: hole punching, probing %i addresses of the peer\n", mc->peer_addrs_count);

	media_send_probes(m, m->punch_started);

	return PICA_OK;
}

void PICA_media_start(struct PICA_c2c *c2c)
{
	PICA_TRACEFUNC

	if (c2c->media || !c2c->acc->dtls_ctx)
		return;

	if (media_direct_start(c2c) == PICA_OK)
		return;

	media_punch_start(c2c);
}

void PICA_media_close(struct PICA_c2c *c2c)
{
	struct PICA_media_channel *m = c2c->media;

	PICA_TRACEFUNC

	if (!m)
		return;

	c2c->media = NULL;

	if (m->peer_cert)
		X509_free(m->peer_cert);

	if (m->ssl)
		SSL_free(m->ssl);

	if (m->owns_sck && m->sck >= 0)
		CLOSE(m->sck);

	if (m->listener)
	{
		m->listener->channel = NULL;

		/* the socket was connected to the peer while the call lasted */
		if (m->state != PICA_MEDIA_STATE_LISTENING)
			media_listener_rearm(m->listener);
	}

	free(m);
}

static void media_fail(struct PICA_media_channel *m)
{
	struct PICA_c2c *c2c = m->c2c;
	/* attempt 2 follows attempt 1 that did not succeed, see PICA_media.h */
	int try_punching = m->attempt == 1 && m->state != PICA_MEDIA_STATE_ACTIVE;

	PICA_TRACEFUNC

	PICA_media_close(c2c);

	if (try_punching && media_punch_start(c2c) == PICA_OK)
		return;

	/* the call carries on, its media goes back to the c2c connection */
	PICA_media_transport_changed(c2c, PICA_CALL_TRANSPORT_C2C, NULL);
}

int PICA_media_is_active(const struct PICA_c2c *c2c)
{
	return c2c->media && c2c->media->state == PICA_MEDIA_STATE_ACTIVE;
}

unsigned int PICA_media_max_payload(const struct PICA_c2c *c2c)
{
	unsigned int mtu;

	if (!PICA_media_is_active(c2c))
		return PICA_PROTO_C2CMSG_MAXDATASIZE - PICA_PROTO_CALL_PACKET_HDRSIZE;

	mtu = c2c->media->max_payload;

	if (mtu <= 4 + PICA_PROTO_CALL_PACKET_HDRSIZE)
		return 0;

	/* the message header, its length field and the media packet header all
	 * have to fit into the same datagram
	 */
	return mtu - 4 - PICA_PROTO_CALL_PACKET_HDRSIZE;
}

int PICA_media_send(struct PICA_c2c *c2c, const unsigned char *buf, unsigned int len)
{
	struct PICA_media_channel *m = c2c->media;
	int ret;

	if (!PICA_media_is_active(c2c))
		return PICA_ERRINVARG;

	ret = SSL_write(m->ssl, buf, len);

	if (ret > 0)
	{
		m->last_tx = time(0);
		return PICA_OK;
	}

	/* The send buffer is full, or the system reports an error such as an
	 * ICMP message caused by an earlier datagram - either way the packet is
	 * dropped rather than queued. Delaying live media is worse than losing
	 * a frame of it, and the receiving side is built to cope with losses
	 * anyway. DTLS drops a record that could not be written and carries on
	 * with the next one, so none of this ends the connection.
	 */
	ERR_clear_error();

	return PICA_OK;
}

static void media_activate(struct PICA_media_channel *m)
{
	int ret;

	PICA_TRACEFUNC

	ret = PICA_verify_peer_cert_common(&m->peer_cert, m->ssl, m->c2c->peer_id);

	if (ret != PICA_OK)
	{
		fprintf(stderr, "media connection peer certificate verification failed: %i\n", ret);
		media_fail(m);
		return;
	}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	m->max_payload = (unsigned int)DTLS_get_data_mtu(m->ssl);
#else
	/* DTLS record header, explicit nonce and authentication tag of the
	 * ciphersuites Pica Pica uses
	 */
	m->max_payload = PICA_MEDIA_LINK_MTU - PICA_MEDIA_IPUDP_OVERHEAD - 13 - 8 - 16;
#endif

	m->state = PICA_MEDIA_STATE_ACTIVE;

	{
		char addrstr[PICA_MEDIA_ADDRSTRSIZE];

		fprintf(stderr, "media: connection with %s established by attempt %i\n",
		        media_addrstr(&m->peer_addr, addrstr), m->attempt);
	}

	PICA_media_transport_changed(m->c2c, PICA_CALL_TRANSPORT_MEDIAC2C, SSL_get_cipher_name(m->ssl));
}

static void media_handshake(struct PICA_media_channel *m)
{
	int ret;

	ret = SSL_do_handshake(m->ssl);

	if (ret == 1)
	{
		media_activate(m);
		return;
	}

	switch (SSL_get_error(m->ssl, ret))
	{
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
		break;

	default:
		fprintf(stderr, "media connection DTLS handshake failed\n");
		media_fail(m);
	}
}

/* The first datagram of a call arrives at the listening socket from a source
 * port only the peer's NAT knows. Once it is seen, the socket is connected to
 * that address for the rest of the call.
 */
static void media_accept(struct PICA_media_channel *m)
{
	struct sockaddr_in from;
#ifdef WIN32
	int fromlen = sizeof(from);
#else
	socklen_t fromlen = sizeof(from);
#endif
	/* Large enough for any datagram the peer can send us, so that the peek
	 * never truncates: a truncated peek is reported as an error on Windows.
	 */
	unsigned char peekbuf[PICA_MEDIA_READBUFSIZE];
	int ret;

	PICA_TRACEFUNC

	ret = recvfrom(m->sck, (char*)peekbuf, sizeof(peekbuf), MSG_PEEK, (struct sockaddr*)&from, &fromlen);

	if (ret < 0)
		return;

	/* The peer of the call is already known - it is the client on the other
	 * end of the directc2c connection - so a datagram from any other address
	 * cannot belong to this call. Only the address is checked here, the port
	 * number is whatever the peer's system picked.
	 */
	if (from.sin_addr.s_addr != m->c2c->direct->addr.sin_addr.s_addr)
	{
		/* consume it, otherwise it is peeked again on every pass */
		recvfrom(m->sck, (char*)peekbuf, sizeof(peekbuf), 0, (struct sockaddr*)&from, &fromlen);
		fprintf(stderr, "dropped a datagram from an address the call is not with\n");
		return;
	}

	m->peer_addr = from;

	if (connect(m->sck, (struct sockaddr*)&m->peer_addr, sizeof(m->peer_addr)) == SOCKET_ERROR)
	{
		media_fail(m);
		return;
	}

	if (media_ssl_new(m) != PICA_OK)
	{
		media_fail(m);
		return;
	}

	SSL_set_accept_state(m->ssl);

	m->state = PICA_MEDIA_STATE_HANDSHAKE;

	media_handshake(m);
}

/* Whether a datagram is a probe carrying our own token, i.e. one that comes
 * from the peer of this call.
 */
static int is_our_probe(struct PICA_media_channel *m, const unsigned char *buf, int len)
{
	return len == PICA_PROTO_MEDIAPROBE_SIZE
	       && buf[0] == PICA_PROTO_MEDIAPROBE && buf[1] == PICA_PROTO_MEDIAPROBE
	       && memcmp(buf + 2, m->c2c->media_call->token, PICA_PROTO_CALL_TOKENSIZE) == 0;
}

/* The peer's probe came from this address, so this is where its NAT lets
 * our datagrams through to it. From now on the socket only talks to it.
 */
static void media_punch_select(struct PICA_media_channel *m, const struct sockaddr_in *from)
{
	char addrstr[PICA_MEDIA_ADDRSTRSIZE];

	PICA_TRACEFUNC

	fprintf(stderr, "media: hole punching, the peer's probe arrived from %s after %u ms, selected\n",
	        media_addrstr(from, addrstr), (unsigned int)(PICA_media_now() - m->punch_started));

	m->peer_addr = *from;

	if (connect(m->sck, (const struct sockaddr*)&m->peer_addr, sizeof(m->peer_addr)) == SOCKET_ERROR)
	{
		media_fail(m);
		return;
	}

	m->deadline = PICA_media_now() + PICA_MEDIA_PUNCH_HANDSHAKE_TIMEOUT;

	if (!m->is_dtls_client)
	{
		/* the caller learns that it can reach us from our probes */
		m->state = PICA_MEDIA_STATE_SELECTED;
		m->last_probe = 0;
		media_send_probes(m, PICA_media_now());
		return;
	}

	if (media_ssl_new(m) != PICA_OK)
	{
		media_fail(m);
		return;
	}

	SSL_set_connect_state(m->ssl);

	m->state = PICA_MEDIA_STATE_HANDSHAKE;
	m->last_probe = 0;
	media_send_probes(m, PICA_media_now());

	media_handshake(m);
}

/* Reads what has arrived while no address is selected yet. Only a probe
 * with our token counts; a DTLS datagram arriving this early is dropped and
 * retransmitted by the peer later on, anything else is not from the peer.
 */
static void media_punch_read(struct PICA_media_channel *m)
{
	unsigned char buf[PICA_MEDIA_READBUFSIZE];
	struct sockaddr_in from;
#ifdef WIN32
	int fromlen;
#else
	socklen_t fromlen;
#endif
	int ret;

	while (1)
	{
		fromlen = sizeof(from);

		ret = recvfrom(m->sck, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);

		if (ret < 0)
		{
#ifdef WIN32
			/* Windows reports an ICMP port unreachable, caused by a probe
			 * sent to an address where nothing listens, on the next
			 * recvfrom() of the socket - which says nothing about the
			 * datagrams queued behind it.
			 */
			if (WSAGetLastError() == WSAECONNRESET)
				continue;
#endif
			return;
		}

		if (from.sin_family == AF_INET && is_our_probe(m, buf, ret))
		{
			/* can free m */
			media_punch_select(m, &from);
			return;
		}
	}
}

/* Takes off the socket, before DTLS gets to read it:
 * - the probes that are still arriving after an address was selected - a
 * DTLS record never starts with the octet of a probe;
 * - errors the system reports for earlier datagrams, e.g. an ICMP port
 * unreachable from the peer's NAT. Reading one clears it, and left to DTLS
 * it would be taken for the end of the connection.
 * \return 1 if a datagram other than a probe is waiting
 */
static int media_skip_probes(struct PICA_media_channel *m)
{
	unsigned char buf[PICA_MEDIA_READBUFSIZE];
	/* an error that does not clear itself must not keep us here */
	int errors = 0;
	int ret;

	while (1)
	{
		/* Large enough for any datagram, so that the peek never truncates:
		 * a truncated peek is reported as an error on Windows.
		 */
		ret = recv(m->sck, (char*)buf, sizeof(buf), MSG_PEEK);

		if (ret < 0)
		{
#ifdef WIN32
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				return 0;
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
#endif
			if (++errors > 16)
				return 0;

			continue;
		}

		if (ret == 0 || buf[0] == PICA_PROTO_MEDIAPROBE)
		{
			recv(m->sck, (char*)buf, sizeof(buf), 0);
			continue;
		}

		m->dtls_seen = 1;

		return 1;
	}
}

/* Attempt 2, the callee: the caller's handshake has started arriving. */
static void media_punch_accept(struct PICA_media_channel *m)
{
	PICA_TRACEFUNC

	if (media_ssl_new(m) != PICA_OK)
	{
		media_fail(m);
		return;
	}

	SSL_set_accept_state(m->ssl);

	m->state = PICA_MEDIA_STATE_HANDSHAKE;

	media_handshake(m);
}

static void media_read(struct PICA_media_channel *m)
{
	struct PICA_c2c *c2c = m->c2c;
	int ret;

	/* One SSL_read() per datagram, so this loop runs until the socket has
	 * nothing left for us.
	 */
	while (1)
	{
		/* a late probe of a hole punched connection, or a socket error */
		if (SSL_pending(m->ssl) == 0 && !media_skip_probes(m))
			return;

		ret = SSL_read(m->ssl, m->read_buf, PICA_MEDIA_READBUFSIZE);

		if (ret <= 0)
		{
			switch (SSL_get_error(m->ssl, ret))
			{
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				return;

			case SSL_ERROR_SYSCALL:
				/* A socket error that arrived after media_skip_probes()
				 * looked. It says nothing about the connection, the same as
				 * a lost datagram.
				 */
				ERR_clear_error();
				return;

			default:
				/* the peer closed the connection or sent a fatal alert */
				fprintf(stderr, "media connection read failed\n");
				media_fail(m);
				return;
			}
		}

		m->read_pos = (unsigned int)ret;

		/* A datagram carries exactly one message. Anything that does not
		 * parse is dropped together with the rest of the datagram - unlike
		 * on a c2c connection, bad data here says nothing about the peer,
		 * anyone able to reach the port can send it.
		 */
		PICA_media_process_messages(m->read_buf, &m->read_pos, c2c);

		/* Processing a datagram can close this media channel (e.g. a failed
		 * ping reply calls media_fail(), which frees m). If it did, m is now
		 * dangling - stop before touching it again. m is only compared as an
		 * address here, never dereferenced.
		 */
		if (c2c->media != m)
			return;

		m->read_pos = 0;
	}
}

void PICA_media_fdset(struct PICA_c2n *c2n, fd_set *rfds, int *nfds)
{
	struct PICA_c2c *c2c = c2n->chan_list_head;

	while (c2c)
	{
		if (c2c->media && c2c->media->sck >= 0)
		{
			FD_SET(c2c->media->sck, rfds);

			if (c2c->media->sck > *nfds)
				*nfds = c2c->media->sck;
		}

		c2c = c2c->next;
	}
}

void PICA_media_process(struct PICA_c2n *c2n, fd_set *rfds)
{
	struct PICA_c2c *chan = c2n->chan_list_head;
	time_t now = time(0);
	uint64_t now_ms = PICA_media_now();

	while (chan)
	{
		struct PICA_c2c *c2c = chan;
		struct PICA_media_channel *m = c2c->media;
		int readable;

		chan = chan->next;

		if (!m)
			continue;

		if (m->state != PICA_MEDIA_STATE_ACTIVE && now_ms >= m->deadline)
		{
			fprintf(stderr, "media connection attempt %i was not completed in time\n", m->attempt);
			media_fail(m);
			continue;
		}

		readable = m->sck >= 0 && FD_ISSET(m->sck, rfds);

		/* Anything below that can close the media connection - which frees m,
		 * and can start another attempt in its place - is the last thing
		 * done with m in this pass, or is followed by a check through c2c,
		 * which stays valid.
		 */
		switch (m->state)
		{
		case PICA_MEDIA_STATE_LISTENING:
			if (readable)
				media_accept(m);
			break;

		case PICA_MEDIA_STATE_PUNCHING:
			media_send_probes(m, now_ms);

			if (readable)
				media_punch_read(m);
			break;

		case PICA_MEDIA_STATE_SELECTED:
			media_send_probes(m, now_ms);

			if (readable && media_skip_probes(m))
				media_punch_accept(m);
			break;

		case PICA_MEDIA_STATE_HANDSHAKE:
			if (m->attempt == 2)
				media_send_probes(m, now_ms);

			/* Lost handshake messages are retransmitted by OpenSSL, but
			 * only when it is told that its timer has expired.
			 */
			DTLSv1_handle_timeout(m->ssl);

			if (readable && (m->attempt != 2 || media_skip_probes(m)))
				media_handshake(m);
			break;

		case PICA_MEDIA_STATE_ACTIVE:
			if (readable)
				media_read(m);

			if (!PICA_media_is_active(c2c))
				break;

			if ((now - m->last_tx) >= PICA_MEDIA_KEEPALIVE_INTERVAL)
				PICA_media_send_ping(c2c, 0);

			break;

		default:
			break;
		}
	}
}
