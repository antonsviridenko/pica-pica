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

#ifndef PICA_CLIENT_H
#define PICA_CLIENT_H

#include <time.h>
#include "PICA_id.h"

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

typedef unsigned __int16 uint16_t;
typedef unsigned __int64 uint64_t;

#else

#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
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


#define PICA_C2C_INCOMING 0
#define PICA_C2C_OUTGOING 1
#define PICA_DIRECTC2C_INCOMING 0
#define PICA_DIRECTC2C_OUTGOING 1

struct PICA_c2n;
struct PICA_c2c;

struct PICA_acc
{
	/**
	* SHA224 hash of user's certificate in DER format
	*/
	unsigned char id[PICA_ID_SIZE];
	SSL_CTX* ctx;
	SSL_CTX* anon_ctx;
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

	/** this flag should be set when connection
	*  must be closed after sending last pushed packet
	*/
	int disconnect_on_empty_write_buf;
	int switched_to_directc2c_write;
	int switched_to_directc2c_read;
	int want_write;
	unsigned int directc2c_write_barrier_pos;
	unsigned int directc2c_write_before_barrier_pos;
};

struct PICA_listener
{
	const struct PICA_acc *acc;
	SOCKET  sck_listener;

	int public_port;
	int local_port;

	in_addr_t public_addr_ipv4;
	const char *public_addr_dns;

	struct PICA_directc2c *accepted_connections;
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
	 * Peer'c certificate in PEM format. Callback must compare passed cert with
	 * previously stored cert of this peer (if it exists).
	 * \return verification result
	 * \retval 1 - verification success
	 * \retval 0 - verification failed
	 */
	int (*peer_cert_verify_cb)(const unsigned char *peer_id, const char *cert_pem, unsigned int nb);
	/**
	 * Received request of file transfer.
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
};



#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read certificate from cert_file in PEM format, store id in buffer pointed by id
 * \param [in] cert_file string with certificate file name
 * \param [out] id buffer to store ID, PICA_ID_SIZE octets
 */
int PICA_get_id_from_cert_file(const char *cert_file, unsigned char *id);
/**
 * Read certificate from C string in PEM format, store id in buffer pointed by id.
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
 * Opens socket to listen for incoming direct c2c connections bypassing nodes
 * \param acc - pointer to opened account instance
 * \param public_addr - DNS name or IP address string,
 * it is an address that will be used for connecting from global Internet.
 * Should be set to router's external IP if computer running pica-client is located behind the NAT.
 * \param public_port - TCP port for incoming connections from global Internet
 * \param local_port - TCP port that will be actually opened on computer listening for direct c2c connections
 * \param l - address of pointer that will be filled with address of created PICA_listener structure
 */
int PICA_new_listener(const struct PICA_acc *acc, const char *public_addr, int public_port, int local_port, struct PICA_listener **l);

int PICA_open_acc(const char *cert_file,
                  const char *pkey_file,
                  const char *dh_param_file,
                  int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
                  struct PICA_acc **acc);

//connections, listeners - NULL-terminated arrays of pointers to appropriate structures
// timeout - timeout in milliseconds
int PICA_event_loop(struct PICA_c2n **connections, int timeout);

int PICA_send_msg(struct PICA_c2c *chn, char *buf, unsigned int len);

//filename - ASCII or UTF-8 encoded string
int PICA_send_file(struct PICA_c2c *chn, const char *filepath);
int PICA_accept_file(struct PICA_c2c *chan, char *filename, unsigned int filenamesize);
int PICA_deny_file(struct PICA_c2c *chan);
// if sending != 0 then pause sending file, else pause receiving of file
int PICA_pause_file(struct PICA_c2c *chan, int sending);
int PICA_resume_file(struct PICA_c2c *chan, int sending);
int PICA_cancel_file(struct PICA_c2c *chan, int sending);

void PICA_close_c2c(struct PICA_c2c *chn);
void PICA_close_c2n(struct PICA_c2n *cid);
void PICA_close_directc2c(struct PICA_directc2c *d);
void PICA_close_listener(struct PICA_listener *l);
void PICA_close_acc(struct PICA_acc *a);
#ifdef __cplusplus
}
#endif


#endif
