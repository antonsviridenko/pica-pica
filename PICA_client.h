#ifndef PICA_CLIENT_H
#define PICA_CLIENT_H

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

typedef unsigned __int16 uint16_t;

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

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
#define PICA_ERRVERDIFFER -6
#define PICA_ERRNOTFOUND -7
#define PICA_ERRNOPEERCERT -8
#define PICA_ERRINVPEERCERT -9
#define PICA_ERRDISCONNECT -10
#define PICA_ERRINVCERT -11
#define PICA_ERRMSGSIZE -12
#define PICA_ERRDNSRESOLVE -13

//#define PICA_CHNMSGBUFLEN 104

#define PICA_CHANREADBUFSIZE 65536
#define PICA_CHANWRITEBUFSIZE 4096
#define PICA_CONNREADBUFSIZE 65536
#define PICA_CONNWRITEBUFSIZE 4096

#define PICA_CHANSTATE_ACTIVE 13
#define PICA_CONNSTATE_CONNECTING 17
#define PICA_CONNSTATE_CONNECTED 19

struct PICA_conninfo;
struct PICA_chaninfo;

struct PICA_conninfo
{
//#warning "sockaddr!"
struct sockaddr_in srv_addr; // sockaddr !!
unsigned int id;
SSL_CTX* ctx;
SOCKET sck_comm;
SSL* ssl_comm;
//сертификат клиента
// unsigned char r_buf[128];
// unsigned int r_pos;

int state;

unsigned char *read_buf;
unsigned char *write_buf;
unsigned int read_pos;
unsigned int write_pos;
unsigned int write_buflen;

struct PICA_chaninfo *chan_list_head;
struct PICA_chaninfo *chan_list_end;

int init_resp_ok;
unsigned char node_ver_major, node_ver_minor;
};

struct PICA_chaninfo
{
struct PICA_conninfo *conn;//соединение с сервером, через которое установлен данный логический канал связи
unsigned int peer_id;//номер собеседника
SOCKET sck_data;
SSL *ssl;
int outgoing;//1 если создание канала инициировано локальным клиентом, 0 - если собеседником
X509 *peer_cert;
//unsigned char msgbuf[PICA_CHNMSGBUFLEN];

unsigned char *read_buf;
unsigned char *write_buf;
unsigned int read_pos;
unsigned int write_pos;
unsigned int write_buflen;

struct PICA_chaninfo *next;
struct PICA_chaninfo *prev;
int state;
};

struct PICA_client_callbacks
{
//получение сообщения.
void (*newmsg_cb)(unsigned int peer_id,char* msgbuf,unsigned int nb,int type);
//получение подтверждения о доставке сообщения
void (*msgok_cb)(unsigned int peer_id);
//создание канала с собеседником
void (*channel_established_cb)(unsigned int peer_id);
//создать канал не удалось		
void (*channel_failed)(unsigned int peer_id);
//входящий запрос на создание канала от пользователя с номером caller_id
//возвращаемое значение: 0 - отклонить запрос, ненулевое значение - принять запрос
int (*accept_cb)(unsigned int caller_id);
//запрошенный пользователь не найден, в оффлайне или отказался от общения
void (*notfound_cb)(unsigned int callee_id);
//
void (*channel_closed_cb)(unsigned int peer_id, int reason);

void (*nodelist_cb)(int type, void *addr_bin, const char *addr_str, unsigned int port);
//сертификат собеседника в формате PEM. Функция должна сравнить предъявленный сертификат с сохранённым (если есть) и вернуть 1 при успешной проверке, 0 - при неуспешной
int (*peer_cert_verify_cb)(unsigned int peer_id, const char *cert_pem, unsigned int nb);
};

 	

#ifdef __cplusplus
extern "C" {
#endif

int PICA_get_id_from_cert(const char *cert_file, unsigned int *p_id);
int PICA_client_init(struct PICA_client_callbacks *clcbs);

int PICA_new_connection
	( const char *nodeaddr,
	  unsigned int port, 
	  const char *CA_file,
	  const char *cert_file, 
	  const char *pkey_file, 
          //const char* password,
          int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
	  struct PICA_conninfo **ci);

int PICA_create_channel(struct PICA_conninfo *ci,unsigned int uid,struct PICA_chaninfo **chn);
int PICA_read_c2n(struct PICA_conninfo *ci);

int PICA_read(struct PICA_conninfo *ci,int timeout);
int PICA_write(struct PICA_conninfo *ci);

int PICA_send_msg(struct PICA_chaninfo *chn, char *buf,unsigned int len);
int PICA_read_msg(struct PICA_chaninfo *chn,char *buf,unsigned int *n);

void PICA_close_channel(struct PICA_chaninfo *chn);
void PICA_close_connection(struct PICA_conninfo *cid);

#ifdef __cplusplus
}
#endif


#endif
