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

//#define PICA_CHNMSGBUFLEN 104

#define PICA_CHANREADBUFSIZE 65536
#define PICA_CHANWRITEBUFSIZE 4096
#define PICA_CONNREADBUFSIZE 65536
#define PICA_CONNWRITEBUFSIZE 4096
#define PICA_FILEFRAGMENTSIZE 16384 //max size of TLS record

#define PICA_CHANSTATE_ACTIVE 13
#define PICA_CONNSTATE_CONNECTING 17
#define PICA_CONNSTATE_CONNECTED 19

#define PICA_CHANSENDFILESTATE_IDLE 0
#define PICA_CHANSENDFILESTATE_SENTREQ 20
#define PICA_CHANSENDFILESTATE_SENDING 22
#define PICA_CHANSENDFILESTATE_PAUSED 23

#define PICA_CHANRECVFILESTATE_IDLE 0
#define PICA_CHANRECVFILESTATE_RECEIVING 24
#define PICA_CHANRECVFILESTATE_PAUSED 25
#define PICA_CHANRECVFILESTATE_WAITACCEPT 26

#define PICA_CHAN_ACTIVATE_TIMEOUT 30


#define PICA_CHANNEL_INCOMING 0
#define PICA_CHANNEL_OUTGOING 1

struct PICA_conninfo;
struct PICA_chaninfo;

struct PICA_conninfo
{
//#warning "sockaddr!"
struct sockaddr_in srv_addr; // sockaddr !!
unsigned char id[PICA_ID_SIZE]; //SHA224 hash of user's certificate in DER format
SSL_CTX* ctx;
SOCKET sck_comm;
SSL* ssl_comm;


int state;

unsigned char *read_buf;
unsigned char *write_buf;
unsigned int read_pos;
unsigned int write_pos;
unsigned int write_buflen;
unsigned int write_sslbytestowrite;

struct PICA_chaninfo *chan_list_head;
struct PICA_chaninfo *chan_list_end;

int init_resp_ok;
unsigned char node_ver_major, node_ver_minor;
};

struct PICA_chaninfo
{
struct PICA_conninfo *conn;//соединение с сервером, через которое установлен данный логический канал связи
unsigned char peer_id[PICA_ID_SIZE];
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
unsigned int write_sslbytestowrite;

struct PICA_chaninfo *next;
struct PICA_chaninfo *prev;
int state;
time_t timestamp;
int sendfilestate;
uint64_t sendfile_size;
uint64_t sendfile_pos;
FILE *sendfile_stream;

int recvfilestate;
uint64_t recvfile_size;
uint64_t recvfile_pos;
FILE *recvfile_stream;
};

struct PICA_client_callbacks
{
//получение сообщения.
void (*newmsg_cb)(const unsigned char *peer_id,const char* msgbuf,unsigned int nb,int type);
//получение подтверждения о доставке сообщения
void (*msgok_cb)(const unsigned char *peer_id);
//создание канала с собеседником
void (*channel_established_cb)(const unsigned char *peer_id);
//создать канал не удалось		
void (*channel_failed)(const unsigned char *peer_id);
//входящий запрос на создание канала от пользователя с номером caller_id
//возвращаемое значение: 0 - отклонить запрос, ненулевое значение - принять запрос
int (*accept_cb)(const unsigned char  *caller_id);
//запрошенный пользователь не найден, в оффлайне или отказался от общения
void (*notfound_cb)(const unsigned char  *callee_id);
//
void (*channel_closed_cb)(const unsigned char *peer_id, int reason);

void (*nodelist_cb)(int type, void *addr_bin, const char *addr_str, unsigned int port);
//сертификат собеседника в формате PEM. Функция должна сравнить предъявленный сертификат с сохранённым (если есть) и вернуть 1 при успешной проверке, 0 - при неуспешной
int (*peer_cert_verify_cb)(const unsigned char *peer_id, const char *cert_pem, unsigned int nb);
// returns 0 if file is rejected, 1 if accepted, 2 if decision is postponed
int (*accept_file_cb)(const unsigned char  *peer_id, uint64_t  file_size, const char *filename, unsigned int filename_size);

void (*accepted_file_cb)(const unsigned char *peer_id);

void (*denied_file_cb)(const unsigned char *peer_id);

void (*file_progress)(const unsigned char *peer_id, uint64_t sent, uint64_t received);

void (*file_control)(const unsigned char *peer_id, unsigned int sender_cmd, unsigned int receiver_cmd);

void (*file_finished)(const unsigned char *peer_id, int sending);
};

 	

#ifdef __cplusplus
extern "C" {
#endif

//read certificate from cert_file in PEM format, store id in buffer pointed by id 
int PICA_get_id_from_cert_file(const char *cert_file, unsigned char *id);
//read certificate from C string in PEM format, store id in buffer pointed by id
int PICA_get_id_from_cert_string(const char *cert_pem, unsigned char *id);
int PICA_client_init(struct PICA_client_callbacks *clcbs);

int PICA_new_connection
    (const char *nodeaddr,
      unsigned int port,
      const char *CA_file,
      const char *cert_file,
      const char *pkey_file,
      const char *dh_param_file,
      int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
      struct PICA_conninfo **ci);

int PICA_create_channel(struct PICA_conninfo *ci,const unsigned char *peer_id,struct PICA_chaninfo **chn);
int PICA_read_c2n(struct PICA_conninfo *ci);

int PICA_read(struct PICA_conninfo *ci,int timeout);
int PICA_write(struct PICA_conninfo *ci);

int PICA_send_msg(struct PICA_chaninfo *chn, char *buf,unsigned int len);
int PICA_read_msg(struct PICA_chaninfo *chn,char *buf,unsigned int *n);

//filename - ASCII or UTF-8 encoded string
int PICA_send_file(struct PICA_chaninfo *chn, const char *filepath);
int PICA_accept_file(struct PICA_chaninfo *chan, char *filename, unsigned int filenamesize);
int PICA_deny_file(struct PICA_chaninfo *chan);
// if sending != 0 then pause sending file, else pause receiving of file
int PICA_pause_file(struct PICA_chaninfo *chan, int sending);
int PICA_resume_file(struct PICA_chaninfo *chan, int sending);

void PICA_close_channel(struct PICA_chaninfo *chn);
void PICA_close_connection(struct PICA_conninfo *cid);

#ifdef __cplusplus
}
#endif


#endif
