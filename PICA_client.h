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

#ifndef PICA_CLIENT_H
#define PICA_CLIENT_H

#include <time.h>
#include "PICA_id.h"
#include "PICA_proto.h"

#ifdef WIN32

#define NOCRYPT
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#if defined(__MINGW32__) || defined(HAVE_STDINT_H)
#include <stdint.h>
#else
typedef unsigned __int8  uint8_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int64 uint64_t;
#endif

typedef u_long in_addr_t;
typedef u_short in_port_t;

#else

#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>

#ifdef PICA_MULTITHREADED
#include <pthread.h>
#endif

#define SOCKET int
#define SOCKET_ERROR -1

#endif

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <stdio.h>

#ifdef PICA_MULTITHREADED

#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#if defined(OPENSSL_THREADS)
// thread support enabled
#else
#error "THREAD SUPPORT IS NOT ENABLED IN OPENSSL!"
#endif

#endif

//#define PICA_MSGOK 3
//#define PICA_MSGUTF8 2
#define PICA_NEWCHAN 1
#define PICA_OK 0
#define PICA_ERRINVARG -1
#define PICA_ERRNOMEM -2
#define PICA_ERRSSL -3
#define PICA_ERRSOCK -4
#define PICA_ERRSERV -5
//#define PICA_ERRVERDIFFER -6
#define PICA_ERRNOTFOUND -7
#define PICA_ERRNOPEERCERT -8
#define PICA_ERRINVPEERCERT -9
#define PICA_ERRDISCONNECT -10
#define PICA_ERRINVCERT -11
#define PICA_ERRMSGSIZE -12
#define PICA_ERRDNSRESOLVE -13

#define PICA_ERRPROTOOLD -14
#define PICA_ERRPROTONEW -15
#define PICA_ERRINVFILENAME -16
#define PICA_ERRFILETRANSFERINPROGRESS -17
#define PICA_ERRFILETRANSFERNOTINPROGRESS -18
#define PICA_ERRFILEFRAGMENTCROSSEDSIZE -19
#define PICA_ERRFILEOPEN -20
#define PICA_ERRFILEIO -21

#define PICA_ERRINVPKEYPASSPHRASE -22
#define PICA_ERRINVPKEYFILE -23
#define PICA_ERRTIMEDOUT -24

#define PICA_ERRCALLINPROGRESS -25
#define PICA_ERRCALLNOTINPROGRESS -26

//#define PICA_CHNMSGBUFLEN 104

#define PICA_CHANREADBUFSIZE 65536
#define PICA_CHANWRITEBUFSIZE 4096
#define PICA_CONNREADBUFSIZE 65536
#define PICA_CONNWRITEBUFSIZE 4096
#define PICA_FILEFRAGMENTSIZE 3 * 16384 //max size of TLS record

#define PICA_CHANSENDFILESTATE_IDLE 0
#define PICA_CHANSENDFILESTATE_SENTREQ 20
#define PICA_CHANSENDFILESTATE_SENDING 22
#define PICA_CHANSENDFILESTATE_PAUSED 23

#define PICA_CHANRECVFILESTATE_IDLE 0
#define PICA_CHANRECVFILESTATE_RECEIVING 24
#define PICA_CHANRECVFILESTATE_PAUSED 25
#define PICA_CHANRECVFILESTATE_WAITACCEPT 26

#define PICA_C2C_ACTIVATE_TIMEOUT 30

/* PICA_request_udp_address(): how long the node's answer is waited for, and
 * how many datagrams are sent to the node, one per second, until it comes.
 */
#define PICA_UDPADDR_TIMEOUT 10
#define PICA_UDPADDR_PROBES 3

/* How long an incoming direct connection may take to finish its TLS handshake
 * before it is reaped, and how many half-open incoming connections may sit in
 * a listener at once - both bound a peer's ability to tie up the listener.
 */
#define PICA_DIRECTC2C_ACCEPT_TIMEOUT 30
#define PICA_DIRECTC2C_MAX_ACCEPTED 32

/* How long an outgoing direct connection may take to connect and finish its
 * TLS handshake before the next announced address is tried, in milliseconds.
 * Without it, an address that silently drops the packets would hold up the
 * addresses after it for as long as the system keeps retrying the
 * connection, minutes.
 */
#define PICA_DIRECTC2C_CONNECT_TIMEOUT 5000

/* How long the TCP connection to a private address may take, in
 * milliseconds. Only one on a private network this host is on is tried at
 * all, and there the round trip is a matter of a few milliseconds; the TLS
 * handshake, which can take longer on a slow machine, then gets the rest of
 * PICA_DIRECTC2C_CONNECT_TIMEOUT.
 */
#define PICA_DIRECTC2C_LAN_CONNECT_TIMEOUT 200

/* Most addresses of a received 0x0B message that are tried, and most that
 * a listener announces.
 */
#define PICA_DIRECTC2C_MAX_ADDRS 32


#define PICA_C2C_INCOMING 0
#define PICA_C2C_OUTGOING 1
#define PICA_DIRECTC2C_INCOMING 0
#define PICA_DIRECTC2C_OUTGOING 1

struct PICA_c2n;
struct PICA_c2c;
struct PICA_media_channel;
struct PICA_media_listener;

struct PICA_acc
{
	/**
	* SHA224 hash of user's certificate in DER format
	*/
	unsigned char id[PICA_ID_SIZE];
	SSL_CTX* ctx;
	SSL_CTX* anon_ctx;
	/** used for mediac2c connections, see PICA_media.h */
	SSL_CTX* dtls_ctx;
	const char *pkey_file;
	const char *cert_file;
	int (*password_cb)(char *buf, int size, int rwflag, void *userdata);
};

enum PICA_c2n_state
{
	PICA_C2N_STATE_NEW = 0,
	PICA_C2N_STATE_CONNECTING,
	PICA_C2N_STATE_WAITINGANONTLS,
	PICA_C2N_STATE_WAITINGREP,
	PICA_C2N_STATE_WAITINGANONTLSSHUTDOWN,
	PICA_C2N_STATE_WAITINGTLS,
	PICA_C2N_STATE_CONNECTED
};

enum PICA_udpaddr_state
{
	PICA_UDPADDR_STATE_IDLE = 0,
	/* request sent, waiting for the node to issue a token */
	PICA_UDPADDR_STATE_WAITINGTOKEN,
	/* sending the token to the node's UDP port, waiting for the address */
	PICA_UDPADDR_STATE_PROBING
};

enum PICA_directc2c_config
{
	PICA_DIRECTC2C_CFG_DISABLED,
	PICA_DIRECTC2C_CFG_CONNECTONLY,
	PICA_DIRECTC2C_CFG_ALLOWINCOMING
};

struct PICA_c2n
{
	struct PICA_acc *acc;
//#warning "sockaddr!"
	struct sockaddr_in srv_addr; // sockaddr !!
	SOCKET sck_comm;
	SSL* ssl_comm;

	enum PICA_c2n_state state;
	enum PICA_directc2c_config directc2c_config;
	int multilogin_policy;
	uint64_t multilogin_last_timestamp;
	struct PICA_listener *directc2c_listener;

	unsigned char *read_buf;
	unsigned char *write_buf;
	unsigned int read_pos;
	unsigned int write_pos;
	unsigned int write_buflen;
	unsigned int write_sslbytestowrite;

	struct PICA_c2c *chan_list_head;
	struct PICA_c2c *chan_list_end;

	int init_resp_ok;
	unsigned char node_ver_major, node_ver_minor;
	int disconnect_on_empty_write_buf;
	int want_write;

	int tcpaddr_requested;

	/* request made by PICA_request_udp_address() */
	enum PICA_udpaddr_state udpaddr_state;
	SOCKET udpaddr_sck;
	unsigned char udpaddr_token[PICA_PROTO_MYUDPADDR_TOKENSIZE];
	time_t udpaddr_started;
	time_t udpaddr_last_probe;
	int udpaddr_probes_sent;
	/* The call the request was made for by the library itself, or NULL when
	 * the application made it. The answer goes to that call instead of
	 * udp_address_cb.
	 */
	struct PICA_c2c *udpaddr_c2c;
};

enum PICA_c2c_state
{
	PICA_C2C_STATE_NEW = 0,
	PICA_C2C_STATE_CONNECTING,
	PICA_C2C_STATE_WAITINGANONTLS,
	PICA_C2C_STATE_CONNID,
	PICA_C2C_STATE_WAITINGANONTLSSHUTDOWN,
	PICA_C2C_STATE_WAITINGTLS,
	PICA_C2C_STATE_WAITINGREP,
	PICA_C2C_STATE_WAITINGC2CPROTOVER,
	PICA_C2C_STATE_ACTIVE
};

enum PICA_directc2c_state
{
	PICA_DIRECTC2C_STATE_INACTIVE,
	PICA_DIRECTC2C_STATE_CONNECTING,
	PICA_DIRECTC2C_STATE_FAILEDTOCONNECT,
	PICA_DIRECTC2C_STATE_WAITINGINCOMING,
	PICA_DIRECTC2C_STATE_ACTIVE
};

enum PICA_directc2c_connection_state
{
	PICA_DIRECTC2C_CONNSTATE_NEW = 0,
	PICA_DIRECTC2C_CONNSTATE_CONNECTING,
	PICA_DIRECTC2C_CONNSTATE_WAITINGTLS,
	PICA_DIRECTC2C_CONNSTATE_ACTIVE,
	PICA_DIRECTC2C_CONNSTATE_FAILED
};

enum PICA_call_state
{
	PICA_CALL_STATE_IDLE = 0,
	PICA_CALL_STATE_CALLING,
	PICA_CALL_STATE_INCOMINGCALL,
	/* Answered by the user, but 0x71 not sent yet: it waits for the node to
	 * report the address of the media socket, see PICA_media_call. Call
	 * parameters set meanwhile are sent right after 0x71, media packets are
	 * dropped.
	 */
	PICA_CALL_STATE_ANSWERING,
	PICA_CALL_STATE_ACTIVE
};

struct PICA_directc2c
{
	int is_outgoing;
	SOCKET sck;
	SSL *ssl;
	X509 *peer_cert;
	int want_write;

	uint8_t *addrlist;
	int addrpos;

	struct sockaddr_in addr;

	enum PICA_directc2c_connection_state state;

	/* PICA_media_now() when the connection to the current address was
	 * started, and when it has to be connected by - the TCP connection, or
	 * in PICA_DIRECTC2C_CONNSTATE_WAITINGTLS the TLS handshake as well, see
	 * PICA_DIRECTC2C_CONNECT_TIMEOUT
	 */
	uint64_t attempt_started;
	uint64_t attempt_deadline;

	/* When this incoming connection was accepted, used to reap it if its TLS
	 * handshake never completes. Only meaningful for accepted (incoming)
	 * connections held in a listener's accepted_connections list.
	 */
	time_t accepted_tmst;

	struct PICA_directc2c *next;
};

struct PICA_c2c
{
	const struct PICA_acc *acc;
	struct PICA_c2n *conn;
	unsigned char peer_id[PICA_ID_SIZE];
	SOCKET sck_data;
	SSL *ssl;

	struct PICA_directc2c *direct;

	int outgoing;
	X509 *peer_cert;

	unsigned char *read_buf;
	unsigned char *write_buf;
	unsigned int read_pos;
	unsigned int write_pos;
	unsigned int write_buflen;
	unsigned int write_sslbytestowrite;

	struct PICA_c2c *next;
	struct PICA_c2c *prev;
	enum PICA_c2c_state state;
	/* the c2n connection's directc2c_config when this connection was made;
	 * the direct connection is negotiated by it, whatever the setting is
	 * changed to later on */
	enum PICA_directc2c_config directc2c_config;
	enum PICA_directc2c_state directc2c_state;
	time_t timestamp;
	int sendfilestate;
	uint64_t sendfile_size;
	uint64_t sendfile_pos;
	FILE *sendfile_stream;

	int recvfilestate;
	uint64_t recvfile_size;
	uint64_t recvfile_pos;
	FILE *recvfile_stream;

	/** this flag should be set when the connection
	*  must be closed after sending the last pushed packet
	*/
	int disconnect_on_empty_write_buf;
	int switched_to_directc2c_write;
	int switched_to_directc2c_read;
	int want_write;
	unsigned int directc2c_write_barrier_pos;
	unsigned int directc2c_write_before_barrier_pos;

	enum PICA_call_state call_state;
	int call_audio_params_are_set;
	int call_video_params_are_set;
	/** media connection of the call in progress, see PICA_media.h */
	struct PICA_media_channel *media;
	/** what the call in progress needs for UDP hole punching, see PICA_media.h */
	struct PICA_media_call *media_call;
};

/* One entry of a manually configured address list, see
 * PICA_listener_set_addrlist()
 */
struct PICA_listener_addr
{
	in_addr_t addr;
	/** network byte order */
	uint16_t port;
	/** stands for the external address and port of the UPnP port mapping */
	int is_upnp;
};

struct PICA_listener
{
	const struct PICA_acc *acc;
	SOCKET  sck_listener;

	/** TCP and UDP port listened on */
	int local_port;

	/** The addresses announced in 0x0B. Automatic configuration when 0: the
	 *  addresses of the network interfaces with local_port, then the UPnP
	 *  mapping. Otherwise the list below, in its order.
	 */
	int addrs_manual;
	struct PICA_listener_addr addrs[PICA_DIRECTC2C_MAX_ADDRS];
	int addrs_count;

	/** external address and port of the UPnP port mapping made for this
	 *  listener, network byte order; 0 when there is none */
	in_addr_t upnp_addr;
	uint16_t upnp_port;

	struct PICA_directc2c *accepted_connections;

	/** listens on the UDP port with the same number, see PICA_media.h.
	 * NULL if that port could not be opened.
	 */
	struct PICA_media_listener *media;
};

struct PICA_client_callbacks
{
	/**
	 * New text message received
	 */
	void (*newmsg_cb)(const unsigned char *peer_id, const char* msgbuf, unsigned int nb, int type);
	/**
	 * Message delivery confirmed
	 */
	void (*msgok_cb)(const unsigned char *peer_id);
	/**
	 * Client-to-client connection established
	 */
	void (*c2c_established_cb)(const unsigned char *peer_id, const char *ciphersuitename);
	/**
	 * Failed to establish client-to-client connection
	 */
	void (*c2c_failed_cb)(const unsigned char *peer_id);
	/** Received incoming client-to-client connection request
	\return action to perform
	\retval 0 - reject request
	\retval non-zero - accept request
	*/
	int (*accept_cb)(const unsigned char  *caller_id);
	/**
	 * Requested client is unavailable or rejected request
	 */
	void (*notfound_cb)(const unsigned char  *callee_id);
	/**
	 * Client-to-client connection is closed
	 */
	void (*c2c_closed_cb)(const unsigned char *peer_id, int reason);
	/**
	 * Received list of node addresses. Callback is called for
	 * each node address entry.
	 * \param type address type, PICA_PROTO_NEWNODE_{IPV4,IPV6,DNS}
	 * \param addr_bin  address in binary form, IPv4 or IPv6 in network byte order, DNS - zero-terminated string, same as addr_str
	 * \param addr_str address in string format, zero-terminated
	 * \param port listening port of node, in host byte order
	 */
	void (*nodelist_cb)(int type, void *addr_bin, const char *addr_str, unsigned int port);
	/**
	 * Peer's certificate in PEM format. Callback must compare passed cert with
	 * previously stored cert of this peer (if it exists).
	 * \return verification result
	 * \retval 1 - verification success
	 * \retval 0 - verification failed
	 */
	int (*peer_cert_verify_cb)(const unsigned char *peer_id, const char *cert_pem, unsigned int nb);
	/**
	 * Received file transfer request.
	 * \return action to perform
	 * \retval 0  - reject file
	 * \retval 1 - accept file
	 * \retval 2 - postpone decision. File can be accepted later by calling PICA_accept_file()
	 */
	int (*accept_file_cb)(const unsigned char  *peer_id, uint64_t  file_size, const char *filename, unsigned int filename_size);

	void (*accepted_file_cb)(const unsigned char *peer_id);

	void (*denied_file_cb)(const unsigned char *peer_id);

	void (*file_progress_cb)(const unsigned char *peer_id, uint64_t sent, uint64_t received);

	void (*file_control_cb)(const unsigned char *peer_id, unsigned int sender_cmd, unsigned int receiver_cmd);

	void (*file_finished_cb)(const unsigned char *peer_id, int sending);

	void (*c2n_established_cb)(struct PICA_c2n *c2n);

	void (*c2n_failed_cb)(struct PICA_c2n *c2n, int error);

	void (*c2n_closed_cb)(struct PICA_c2n *c2n, int error);

	void (*listener_error_cb)(struct PICA_listener *lst, int errorcode);

	void (*multilogin_cb)(uint64_t timestamp, void *addr_bin, const char *addr_str, uint16_t port);

	void (*direct_c2c_established_cb)(const unsigned char *peer_id, const char *ciphersuitename);

	void (*incoming_call_cb)(const unsigned char *peer_id);

	void (*call_picked_up_cb)(const unsigned char *peer_id);

	void (*call_rejected_cb)(const unsigned char *peer_id);

	void (*call_hangup_cb)(const unsigned char *peer_id);

	void (*call_audio_params_cb)(const unsigned char *peer_id, const char *codec, uint16_t sample_rate);

	void (*call_video_params_cb)(const unsigned char *peer_id, const char *codec, uint16_t width, uint16_t height);

	void (*call_audio_packet_cb)(const unsigned char *peer_id, uint16_t seq_num, uint32_t timestamp, uint16_t size, const char *pkt_data);

	void (*call_video_packet_cb)(const unsigned char *peer_id, uint16_t seq_num, uint32_t timestamp, uint16_t size, const char *pkt_data);

	/**
	 * The media packets of the call in progress started or stopped being
	 * carried over a mediac2c connection.
	 * \param transport PICA_CALL_TRANSPORT_C2C or PICA_CALL_TRANSPORT_MEDIAC2C
	 * \param ciphersuitename ciphersuite of the media connection, NULL when
	 * the transport is not mediac2c
	 * \param max_payload largest amount of data that can be passed to
	 * PICA_send_audio_packet()/PICA_send_video_packet() from now on
	 */
	void (*call_media_transport_cb)(const unsigned char *peer_id, int transport,
	                                const char *ciphersuitename, unsigned int max_payload);

	/* The callbacks below are optional and can be left NULL. */

	/**
	 * The node's answer to PICA_request_tcp_address(): the address and port
	 * the c2n connection comes from, as the node sees them.
	 * \param type address type, PICA_PROTO_MYTCPADDR_{IPV4,IPV6}
	 * \param addr_bin address in binary form, network byte order
	 * \param addr_str address in string format, zero-terminated
	 * \param port port number, in host byte order
	 */
	void (*tcp_address_cb)(struct PICA_c2n *c2n, int type, void *addr_bin, const char *addr_str, uint16_t port);

	/**
	 * The outcome of PICA_request_udp_address().
	 * \param status PICA_OK - the node saw a datagram from the socket coming
	 * from the address described by the remaining arguments, which have the
	 * same meaning as in tcp_address_cb, except that type is
	 * PICA_PROTO_MYUDPADDR_{IPV4,IPV6}. PICA_ERRTIMEDOUT - no answer; the
	 * node does not offer this, or UDP does not get through to it. The
	 * remaining arguments are zero then.
	 *
	 * The address is what the node saw, and is not authenticated: it is only
	 * good as a place for a peer to try, never as a proof of who is there.
	 */
	void (*udp_address_cb)(struct PICA_c2n *c2n, int status, int type, void *addr_bin, const char *addr_str, uint16_t port);
};



#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read certificate from cert_file in PEM format, store id in buffer pointed to by id
 * \param [in] cert_file string with certificate file name
 * \param [out] id buffer to store ID, PICA_ID_SIZE octets
 */
int PICA_get_id_from_cert_file(const char *cert_file, unsigned char *id);
/**
 * Read certificate from C string in PEM format, store id in buffer pointed to by id.
 * \param [in] cert_pem - certificate in PEM format
 * \param [out] id buffer to store ID, PICA_ID_SIZE octets
 */
int PICA_get_id_from_cert_string(const char *cert_pem, unsigned char *id);
int PICA_client_init(struct PICA_client_callbacks *clcbs);
int PICA_new_c2n(const struct PICA_acc *acc, const char *nodeaddr, unsigned int port,
                 enum PICA_directc2c_config direct_c2c_mode, int multilogin,
                 struct PICA_listener *l, struct PICA_c2n **ci);

int PICA_new_c2c(struct PICA_c2n *ci, const unsigned char *peer_id, struct PICA_listener *l, struct PICA_c2c **chn);

/**
 * Changes the direct connection setting of an established or connecting c2n
 * connection, as given to PICA_new_c2n(). It applies to the c2c connections
 * made from then on; those already there carry on as they were negotiated.
 * \param l the listener for PICA_DIRECTC2C_CFG_ALLOWINCOMING, ignored
 * otherwise. A listener taken away from every c2n connection can be closed.
 */
void PICA_c2n_set_directc2c(struct PICA_c2n *c2n, enum PICA_directc2c_config direct_c2c_mode,
                            struct PICA_listener *l);
/**
 * Opens the TCP port for incoming direct c2c connections bypassing nodes, and
 * the UDP port with the same number for the media of calls.
 *
 * The addresses announced to peers are configured automatically until
 * PICA_listener_set_addrlist() says otherwise: the addresses of the network
 * interfaces, private ones first, with local_port, followed by the UPnP
 * mapping once PICA_listener_set_upnp_addr() reports one.
 *
 * \param acc - pointer to opened account instance
 * \param local_port - port number to listen on, TCP and UDP
 * \param l - address of pointer that will be filled with address of created PICA_listener structure
 * \retval PICA_ERRSOCK one of the two ports could not be opened, e.g. it is
 * in use already
 */
int PICA_new_listener(const struct PICA_acc *acc, int local_port, struct PICA_listener **l);

/**
 * Sets the addresses announced to peers manually, in the order given.
 * \param addrs IPv4 addresses or DNS names, the latter resolved here, which
 * can block. A NULL entry stands for the external address of the UPnP port
 * mapping, see PICA_listener_set_upnp_addr(), and is left out while there is
 * none.
 * \param ports the external port number of each address; for the UPnP entry
 * the mapped port is announced instead
 * \param count number of entries, up to PICA_DIRECTC2C_MAX_ADDRS; 0 goes
 * back to automatic configuration
 * \retval PICA_ERRDNSRESOLVE an address could not be resolved; the list is
 * left as it was
 */
int PICA_listener_set_addrlist(struct PICA_listener *l, const char *const *addrs, const int *ports, int count);

/**
 * Reports the external address and port of a UPnP port mapping made for
 * this listener, or that there is none any more when addr is NULL. Takes
 * effect for the 0x0B messages sent from then on.
 */
int PICA_listener_set_upnp_addr(struct PICA_listener *l, const char *addr, int port);

int PICA_open_acc(const char *cert_file,
                  const char *pkey_file,
                  const char *dh_param_file,
                  int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
                  struct PICA_acc **acc);

//connections, listeners - NULL-terminated arrays of pointers to appropriate structures
// timeout - timeout in milliseconds
int PICA_event_loop(struct PICA_c2n **connections, int timeout);

/**
 * Asks the node which address and TCP port the c2n connection comes from.
 * The answer is delivered to tcp_address_cb.
 */
int PICA_request_tcp_address(struct PICA_c2n *c2n);
/**
 * Asks the node which address and port the datagrams sent from udp_sck come
 * from, i.e. what the NAT in front of this host maps that socket to. The
 * outcome is delivered to udp_address_cb.
 * \param udp_sck bound, unconnected IPv4 UDP socket - the one the address is
 * wanted for. It is only sent from, and has to stay open until the callback
 * is called. The mapping learned is only useful with this same socket.
 *
 * One request per c2n connection can be in progress; another call restarts
 * it. A node serves only a few of these in a row from one client.
 */
int PICA_request_udp_address(struct PICA_c2n *c2n, SOCKET udp_sck);

int PICA_send_msg(struct PICA_c2c *chn, char *buf, unsigned int len);

//filename - ASCII or UTF-8 encoded string
int PICA_send_file(struct PICA_c2c *chn, const char *filepath);
int PICA_accept_file(struct PICA_c2c *chan, char *filename, unsigned int filenamesize);
int PICA_deny_file(struct PICA_c2c *chan);
// if sending != 0 then pause sending file, else pause receiving file
int PICA_pause_file(struct PICA_c2c *chan, int sending);
int PICA_resume_file(struct PICA_c2c *chan, int sending);
int PICA_cancel_file(struct PICA_c2c *chan, int sending);

int PICA_start_call(struct PICA_c2c *chn);
int PICA_pickup_call(struct PICA_c2c *chn);
int PICA_reject_call(struct PICA_c2c *chn);
int PICA_hangup_call(struct PICA_c2c *chn);
int PICA_set_call_audio_params(struct PICA_c2c *chn, const char *codec_name, uint16_t sample_rate);
int PICA_set_call_video_params(struct PICA_c2c *chn, const char *codec_name, uint16_t width, uint16_t height);
int PICA_send_audio_packet(struct PICA_c2c *chn, uint16_t seq_num, uint32_t timestamp, const char *buf, unsigned int len);
int PICA_send_video_packet(struct PICA_c2c *chn, uint16_t seq_num, uint32_t timestamp, const char *buf, unsigned int len);

void PICA_close_c2c(struct PICA_c2c *chn);
void PICA_close_c2n(struct PICA_c2n *cid);
void PICA_close_directc2c(struct PICA_directc2c *d);
void PICA_close_listener(struct PICA_listener *l);
void PICA_close_acc(struct PICA_acc *a);
#ifdef __cplusplus
}
#endif


#endif
