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
 * A mediac2c connection exists only while a call is in progress. When the
 * call becomes active, up to two attempts are made, one after another - see
 * "Establishing the media connection" in doc/proto-doc-latest.txt:
 *
 * 1. Over the directc2c connection's address and port number, with UDP
 * instead of TCP, if the call's c2c connection has an established directc2c
 * one: the client that made the outgoing directc2c connection is the DTLS
 * client, the one that accepted it is the DTLS server, listening on its
 * PICA_media_listener.
 *
 * 2. By UDP hole punching between the media sockets the two clients opened
 * for the call (PICA_media_call), over the addresses exchanged in 0x70 and
 * 0x71. The caller is the DTLS client.
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

/* The time limits of the two attempts, in milliseconds. A failed attempt 1
 * is followed by attempt 2; once that fails too, the media stays on the c2c
 * connection for the rest of the call.
 */
/* attempt 1, from the call becoming active to the handshake completing */
#define PICA_MEDIA_DIRECT_TIMEOUT 5000
/* attempt 2: probes are sent this often... */
#define PICA_MEDIA_PROBE_INTERVAL 200
/* ...for at most this long, until an address is selected */
#define PICA_MEDIA_PUNCH_TIMEOUT 10000
/* and the handshake has this long to complete once it is */
#define PICA_MEDIA_PUNCH_HANDSHAKE_TIMEOUT 10000

/* How long 0x70 and 0x71 wait for the node to report the address of the
 * media socket before they are sent without it, in milliseconds.
 */
#define PICA_MEDIA_LEARN_TIMEOUT 2000

/* An established media connection with no datagram received for this many
 * seconds is considered broken; media falls back to the c2c connection.
 */
#define PICA_MEDIA_SILENCE_TIMEOUT 10

/* Ping is sent over an established media connection after this many seconds
 * without anything being sent, to hold NAT and firewall state open.
 */
#define PICA_MEDIA_KEEPALIVE_INTERVAL 4

#define PICA_MEDIA_READBUFSIZE 4096

/* largest tail of 0x70/0x71 after the length field: the token and the
 * addresses, IPv4 ones being the only kind sent
 */
#define PICA_MEDIA_CALL_INFO_MAXSIZE (PICA_PROTO_CALL_TOKENSIZE + PICA_PROTO_CALL_MAXADDRS * PICA_PROTO_DIRECTC2C_ADDRLIST_ITEM_IPV4_SIZE)

/* transport of the media packets of a call, reported by call_media_transport_cb */
#define PICA_CALL_TRANSPORT_C2C 0
#define PICA_CALL_TRANSPORT_MEDIAC2C 1

enum PICA_media_state
{
	PICA_MEDIA_STATE_NEW = 0,
	/* attempt 1, DTLS server role, socket bound, waiting for the first
	 * datagram */
	PICA_MEDIA_STATE_LISTENING,
	/* attempt 2, sending probes to every address of the peer, waiting for
	 * one of its probes to arrive */
	PICA_MEDIA_STATE_PUNCHING,
	/* attempt 2, DTLS server role, address selected, waiting for the first
	 * datagram of the handshake from it */
	PICA_MEDIA_STATE_SELECTED,
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

/* Everything a call needs for hole punching, from the moment it starts to
 * ring until it ends. Created by PICA_media_call_prepare(), freed by
 * PICA_media_call_free().
 */
struct PICA_media_call
{
	/* the call socket, -1 if this side does not take part in hole punching */
	SOCKET sck;
	/* its port number, host byte order */
	int local_port;
	int is_caller;

	unsigned char token[PICA_PROTO_CALL_TOKENSIZE];

	/* the node is being asked for the address of sck */
	int learning;
	uint64_t learn_started;
	/* what it answered, valid when learned is set */
	int learned;
	struct sockaddr_in learned_addr;

	/* PICA_PROTO_CALLREQ or PICA_PROTO_CALLANS while that message waits for
	 * the address, 0 otherwise */
	int pending_msg;
	uint64_t pending_deadline;

	/* from the peer's 0x70 or 0x71 */
	int peer_info_received;
	unsigned char peer_token[PICA_PROTO_CALL_TOKENSIZE];
	struct sockaddr_in peer_addrs[PICA_PROTO_CALL_MAXADDRS];
	int peer_addrs_count;

	/* call parameters set while the call was PICA_CALL_STATE_ANSWERING */
	int audio_params_pending;
	char audio_codec[PICA_PROTO_CALL_CODECNAMESIZE + 1];
	uint16_t audio_sample_rate;
	int video_params_pending;
	char video_codec[PICA_PROTO_CALL_CODECNAMESIZE + 1];
	uint16_t video_width;
	uint16_t video_height;
};

struct PICA_media_channel
{
	struct PICA_c2c *c2c;
	/* 1 - over the directc2c address, 2 - by hole punching */
	int attempt;
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
	/* when the current stage of setting the connection up has to be
	 * complete, PICA_media_now() milliseconds */
	uint64_t deadline;

	/* attempt 2 */
	uint64_t last_probe;
	/* the first DTLS datagram has arrived from the selected address, the
	 * peer no longer needs probes */
	int dtls_seen;
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

/** Milliseconds from an arbitrary point, never going backwards. */
uint64_t PICA_media_now(void);

/**
 * Starts preparing for hole punching: opens the call socket and asks the
 * node for its address - for the caller before it sends 0x70, for the
 * callee when 0x70 arrives. Without hole punching, e.g. with direct
 * connections disabled, only creates c2c->media_call, whose address list is
 * then empty.
 * \param peer_info for the callee, the tail of the caller's 0x70 after the
 * length field, see PICA_media_call_read_info(); NULL for the caller
 */
int PICA_media_call_prepare(struct PICA_c2c *c2c, int is_caller,
                            const unsigned char *peer_info, unsigned int peer_info_len);

/** The node's answer about the call socket's address; status as in
 *  udp_address_cb. */
void PICA_media_call_learned(struct PICA_c2c *c2c, int status, const unsigned char *addr_bin, int type, uint16_t port);

/** Whether 0x70/0x71 can be sent now: the address is known, or will not be. */
int PICA_media_call_ready(struct PICA_c2c *c2c);

/**
 * Writes the probe token and the address list of 0x70/0x71 to buf.
 * \return number of octets written, at most PICA_MEDIA_CALL_INFO_MAXSIZE
 */
unsigned int PICA_media_call_write_info(struct PICA_c2c *c2c, unsigned char *buf);

/**
 * Takes the probe token and the address list from the tail of the peer's
 * 0x70/0x71, after the length field.
 * \return 0 if it is malformed
 */
int PICA_media_call_read_info(struct PICA_c2c *c2c, const unsigned char *buf, unsigned int len);

/** Releases everything PICA_media_call_prepare() made. */
void PICA_media_call_free(struct PICA_c2c *c2c);

/**
 * Brings up a media connection for a call that has just become active, see
 * the two attempts above. Does nothing if neither of them can be made - the
 * media then stays on the c2c connection.
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
