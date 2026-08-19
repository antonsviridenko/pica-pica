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
#include <string.h>
#include <time.h>

#define PICA_DEBUG

#ifdef PICA_DEBUG
#define PICA_TRACEFUNC fprintf(stderr, "%s()\n", __PRETTY_FUNCTION__);
#else
#define PICA_TRACEFUNC
#endif

static void media_fail(struct PICA_media_channel *m);
static void media_handshake(struct PICA_media_channel *m);

static int media_socket_open(SOCKET *s, int local_port)
{
	int flag = 1;
	struct sockaddr_in a;

	*s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (*s == SOCKET_ERROR)
		return PICA_ERRSOCK;

	setsockopt(*s, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag));

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

	if (ml->channel)
	{
		ml->channel->listener = NULL;
		ml->channel->sck = -1;
		ml->channel = NULL;
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
	m->last_rx = m->started;
	m->last_tx = m->started;

	return m;
}

void PICA_media_start(struct PICA_c2c *c2c)
{
	struct PICA_media_channel *m;
	int ret;

	PICA_TRACEFUNC

	if (c2c->media)
		return;

	/* Media over UDP is only attempted between clients that already reached
	 * each other directly over TCP: that address is known to work and the
	 * user has already agreed to reveal it by enabling direct connections.
	 */
	if (c2c->directc2c_state != PICA_DIRECTC2C_STATE_ACTIVE || !c2c->direct)
		return;

	if (!c2c->acc->dtls_ctx)
		return;

	m = media_channel_new(c2c);

	if (!m)
		return;

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
			return;
		}

		m->owns_sck = 1;

		if (connect(m->sck, (struct sockaddr*)&m->peer_addr, sizeof(m->peer_addr)) == SOCKET_ERROR)
		{
			CLOSE(m->sck);
			free(m);
			return;
		}

		if (media_ssl_new(m) != PICA_OK)
		{
			CLOSE(m->sck);
			free(m);
			return;
		}

		SSL_set_connect_state(m->ssl);

		m->state = PICA_MEDIA_STATE_HANDSHAKE;
		c2c->media = m;

		media_handshake(m);
	}
	else
	{
		struct PICA_media_listener *ml;

		if (!c2c->conn->directc2c_listener || !c2c->conn->directc2c_listener->media)
		{
			free(m);
			return;
		}

		ml = c2c->conn->directc2c_listener->media;

		if (ml->sck < 0 || ml->channel)
		{
			free(m);
			return;
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

	PICA_TRACEFUNC

	PICA_media_close(c2c);

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

	switch (SSL_get_error(m->ssl, ret))
	{
	case SSL_ERROR_WANT_WRITE:
	case SSL_ERROR_WANT_READ:
		/* The send buffer is full - the packet is dropped rather than
		 * queued. Delaying live media is worse than losing a frame of it,
		 * and the receiving side is built to cope with losses anyway.
		 */
		return PICA_OK;

	default:
		media_fail(m);
		return PICA_ERRSSL;
	}
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
	m->last_rx = time(0);

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

static void media_read(struct PICA_media_channel *m)
{
	int ret;

	/* One SSL_read() per datagram, so this loop runs until the socket has
	 * nothing left for us.
	 */
	while (1)
	{
		ret = SSL_read(m->ssl, m->read_buf, PICA_MEDIA_READBUFSIZE);

		if (ret <= 0)
		{
			switch (SSL_get_error(m->ssl, ret))
			{
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				return;

			default:
				fprintf(stderr, "media connection read failed\n");
				media_fail(m);
				return;
			}
		}

		m->last_rx = time(0);
		m->read_pos = (unsigned int)ret;

		/* A datagram carries exactly one message. Anything that does not
		 * parse is dropped together with the rest of the datagram - unlike
		 * on a c2c connection, bad data here says nothing about the peer,
		 * anyone able to reach the port can send it.
		 */
		PICA_media_process_messages(m->read_buf, &m->read_pos, m->c2c);

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

	while (chan)
	{
		struct PICA_c2c *c2c = chan;
		struct PICA_media_channel *m = c2c->media;

		chan = chan->next;

		if (!m)
			continue;

		if (m->state != PICA_MEDIA_STATE_ACTIVE
		        && (now - m->started) > PICA_MEDIA_HANDSHAKE_TIMEOUT)
		{
			fprintf(stderr, "media connection was not established in time\n");
			media_fail(m);
			continue;
		}

		/* Anything below can close the media connection, which frees m -
		 * hence the checks through c2c, which stays valid.
		 */
		switch (m->state)
		{
		case PICA_MEDIA_STATE_LISTENING:
			if (m->sck >= 0 && FD_ISSET(m->sck, rfds))
				media_accept(m);
			break;

		case PICA_MEDIA_STATE_HANDSHAKE:
			/* Lost handshake messages are retransmitted by OpenSSL, but
			 * only when it is told that its timer has expired.
			 */
			DTLSv1_handle_timeout(m->ssl);

			if (m->sck >= 0 && FD_ISSET(m->sck, rfds))
				media_handshake(m);
			break;

		case PICA_MEDIA_STATE_ACTIVE:
			if (m->sck >= 0 && FD_ISSET(m->sck, rfds))
				media_read(m);

			if (!PICA_media_is_active(c2c))
				break;

			if ((now - m->last_rx) > PICA_MEDIA_SILENCE_TIMEOUT)
			{
				fprintf(stderr, "media connection went silent\n");
				media_fail(m);
				break;
			}

			if ((now - m->last_tx) >= PICA_MEDIA_KEEPALIVE_INTERVAL)
				PICA_media_send_ping(c2c, 0);

			break;

		default:
			break;
		}
	}
}
