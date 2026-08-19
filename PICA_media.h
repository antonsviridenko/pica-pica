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

/* mediac2c connections - call media over UDP, protected by DTLS.
 *
 * A mediac2c connection exists only while a call is in progress and only
 * between two clients that already have an established directc2c connection.
 * It reuses that connection's address and port number with UDP instead of
 * TCP, so nothing has to be announced or negotiated over the c2c connection:
 * the client that made the outgoing directc2c connection is the DTLS client,
 * the one that accepted it is the DTLS server.
 *
 * Media packets are sent over this connection whenever it is established and
 * over the c2c/directc2c connection otherwise; the receiving side accepts
 * them over either transport at any time, so there is no moment where the
 * media stops while the transport changes.
 */

#ifndef PICA_MEDIA_H
#define PICA_MEDIA_H

#include "PICA_client.h"

/* Link MTU assumed for a mediac2c connection until path MTU discovery
 * (RFC 8899) is implemented. 1200 octets is the value QUIC uses as the size
 * every path is expected to carry.
 */
#define PICA_MEDIA_LINK_MTU 1200
/* IPv4 + UDP headers */
#define PICA_MEDIA_IPUDP_OVERHEAD 28

/* Amount of media data that certainly fits into one datagram of a mediac2c
 * connection: the assumed link MTU less a generous allowance for the IP, UDP,
 * DTLS record and protocol message headers, whose exact sizes depend on the
 * ciphersuite that ends up being negotiated.
 * It lets the media be produced in datagram sized pieces from the start of a
 * call, before it is known whether a media connection will come up at all,
 * rather than having to change size in the middle of one.
 */
#define PICA_MEDIA_SAFE_PAYLOAD (PICA_MEDIA_LINK_MTU - 100)

/* How long a call keeps trying to bring the media connection up before
 * giving up and carrying the media over the c2c connection for the rest of
 * the call, in seconds.
 */
#define PICA_MEDIA_HANDSHAKE_TIMEOUT 10

/* An established media connection with no datagram received for this many
 * seconds is considered broken; media falls back to the c2c connection.
 */
#define PICA_MEDIA_SILENCE_TIMEOUT 10

/* Ping is sent over an established media connection after this many seconds
 * without anything being sent, to hold NAT and firewall state open.
 */
#define PICA_MEDIA_KEEPALIVE_INTERVAL 4

#define PICA_MEDIA_READBUFSIZE 4096

/* transport of the media packets of a call, reported by call_media_transport_cb */
#define PICA_CALL_TRANSPORT_C2C 0
#define PICA_CALL_TRANSPORT_MEDIAC2C 1

enum PICA_media_state
{
	PICA_MEDIA_STATE_NEW = 0,
	/* DTLS server role, socket bound, waiting for the first datagram */
	PICA_MEDIA_STATE_LISTENING,
	PICA_MEDIA_STATE_HANDSHAKE,
	PICA_MEDIA_STATE_ACTIVE,
	PICA_MEDIA_STATE_FAILED
};

struct PICA_media_listener
{
	const struct PICA_acc *acc;
	SOCKET sck;
	int public_port;
	int local_port;

	/* The media connection currently using this listener's socket, if any.
	 * Only one call can be in progress at a time, so there is at most one.
	 */
	struct PICA_media_channel *channel;
};

struct PICA_media_channel
{
	struct PICA_c2c *c2c;
	/* set when this side has the DTLS server role */
	struct PICA_media_listener *listener;

	SOCKET sck;
	/* 0 when sck belongs to the listener and must not be closed with the
	 * channel
	 */
	int owns_sck;
	struct sockaddr_in peer_addr;

	SSL *ssl;
	X509 *peer_cert;
	int is_dtls_client;

	enum PICA_media_state state;
	time_t started;
	time_t last_rx;
	time_t last_tx;

	unsigned int max_payload;

	unsigned char read_buf[PICA_MEDIA_READBUFSIZE];
	unsigned int read_pos;
};

#ifdef __cplusplus
extern "C" {
#endif

/* implemented in PICA_media.c */

int PICA_media_listener_new(const struct PICA_acc *acc, int public_port, int local_port,
                            struct PICA_media_listener **ml);
void PICA_media_listener_close(struct PICA_media_listener *ml);

/**
 * Brings up a media connection for a call that has just become active.
 * Does nothing if the preconditions (an established directc2c connection,
 * and a media listener on the accepting side) are not met - the media then
 * stays on the c2c connection.
 */
void PICA_media_start(struct PICA_c2c *c2c);
void PICA_media_close(struct PICA_c2c *c2c);

int PICA_media_is_active(const struct PICA_c2c *c2c);
/**
 * Largest amount of data one media packet can carry over this c2c connection,
 * i.e. what has to fit into one datagram once a media connection is up.
 */
unsigned int PICA_media_max_payload(const struct PICA_c2c *c2c);

/**
 * Sends one already formatted protocol message over the media connection.
 * The message is dropped, and PICA_OK returned, if the send would block -
 * for real time media that is the wanted behaviour.
 */
int PICA_media_send(struct PICA_c2c *c2c, const unsigned char *buf, unsigned int len);

void PICA_media_fdset(struct PICA_c2n *c2n, fd_set *rfds, int *nfds);
void PICA_media_process(struct PICA_c2n *c2n, fd_set *rfds);

/* implemented in PICA_client.c, used by PICA_media.c */

int PICA_verify_callback(int preverify_ok, X509_STORE_CTX *ctx);
int PICA_verify_peer_cert_common(X509 **peer_cert, SSL *ssl, const unsigned char *peer_id);
unsigned int PICA_media_process_messages(unsigned char *buf, unsigned int *read_pos, struct PICA_c2c *c2c);
void PICA_media_transport_changed(struct PICA_c2c *c2c, int transport, const char *ciphersuitename);
int PICA_media_send_ping(struct PICA_c2c *c2c, int reply);

#ifdef __cplusplus
}
#endif

#endif
