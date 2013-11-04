#include "PICA_client.h"
#include "PICA_proto.h"
#include "PICA_msgproc.h"
#include "PICA_common.h"
#include <openssl/dh.h>
//#ifdef NO_RAND_DEV
//#include "PICA_rand_seed.h"
//#endif

#define PICA_DEBUG // debug

#ifdef PICA_DEBUG
#include <stdio.h>
#endif

//static SSL_CTX* ctx;
//unsigned char __hellomsg[4]={0xCA,0xCA,PICA_PROTO_VER_HIGH,PICA_PROTO_VER_LOW};

//char _msgbuf[65536];//

static struct PICA_client_callbacks callbacks;

#ifdef PICA_MULTITHREADED

#ifdef WIN32
static HANDLE *mt_locks;

const char *PICA_inet_ntop(int af, const void *src, char *dst, size_t size);
#define inet_ntop PICA_inet_ntop //own implementation of inet_ntop() for WinXP compatibility

#else
static pthread_mutex_t *mt_locks;
#endif

#endif
static unsigned int procmsg_INITRESP(unsigned char*,unsigned int,void*);
static unsigned int procmsg_CONNREQINC(unsigned char*,unsigned int,void*);
static unsigned int procmsg_NOTFOUND(unsigned char*,unsigned int,void*);
static unsigned int procmsg_FOUND(unsigned char*,unsigned int,void*);
static unsigned int procmsg_CLNODELIST(unsigned char*,unsigned int,void*);
static unsigned int procmsg_MSGUTF8(unsigned char*,unsigned int,void*);
static unsigned int procmsg_MSGOK(unsigned char*,unsigned int,void*);
static unsigned int procmsg_PINGREQ(unsigned char*,unsigned int,void*);

static struct PICA_proto_msg* c2n_writebuf_push(struct PICA_conninfo *ci, unsigned int msgid, unsigned int size);
static struct PICA_proto_msg* c2c_writebuf_push(struct PICA_chaninfo *chn, unsigned int msgid, unsigned int size);

#define MSGINFO_MSGSNUM(arr) (sizeof(arr)/sizeof(struct PICA_msginfo))
const struct PICA_msginfo  c2n_messages[] = {
	{PICA_PROTO_CONNREQINC, PICA_MSG_FIXED_SIZE, PICA_PROTO_CONNREQINC_SIZE, procmsg_CONNREQINC},
	{PICA_PROTO_NOTFOUND, PICA_MSG_FIXED_SIZE, PICA_PROTO_NOTFOUND_SIZE, procmsg_NOTFOUND},
	{PICA_PROTO_FOUND, PICA_MSG_FIXED_SIZE, PICA_PROTO_FOUND_SIZE, procmsg_FOUND},
    {PICA_PROTO_CLNODELIST, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_CLNODELIST},
    {PICA_PROTO_PINGREQ, PICA_MSG_FIXED_SIZE, PICA_PROTO_PINGREQ_SIZE, procmsg_PINGREQ}
};//---!!! PING!!!

const struct PICA_msginfo  c2c_messages[] = {
	{PICA_PROTO_MSGUTF8, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_MSGUTF8},
	{PICA_PROTO_MSGOK, PICA_MSG_FIXED_SIZE, PICA_PROTO_MSGOK_SIZE, procmsg_MSGOK}
};

struct PICA_msginfo c2n_init_messages[] = {
    {PICA_PROTO_INITRESPOK, PICA_MSG_FIXED_SIZE, PICA_PROTO_INITRESPOK_SIZE, procmsg_INITRESP},
    {PICA_PROTO_VERDIFFER, PICA_MSG_FIXED_SIZE, PICA_PROTO_VERDIFFER_SIZE, procmsg_INITRESP}
};


// static int pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
//{
//strncpy(buf, (char *)(userdata), size);
//buf[size - 1] = 0;
//return(strlen(buf));
//}
/*
//функция возвращает номер клиента id в бинарном виде, извлекая его из строки,
//которая возвращается функцией X509_NAME_oneline и представляет собой DN из сертификата клиента
 static int get_id_fromsubjstr(char* DN_str,unsigned int* id)
{
char* tmp1;
char* tmp2;

tmp1=strstr(DN_str,"/CN=");

if (!tmp1)
	return 0;

tmp2=strchr(tmp1,'#');

if (!tmp2)
	return 0;

*tmp2=0;

tmp1+=4;

if (tmp1==tmp2)
		return 0;

*id=(unsigned int)strtol(tmp1,0,10);

*tmp2='#';
return 1;
}
*/


int PICA_get_id_from_cert_file(const char *cert_file, unsigned char *id)
{
X509 *x;
FILE *f;

f=fopen(cert_file,"r");

if (!f)
{
	perror(cert_file);
	return 0;
}

x=PEM_read_X509(f,0,0,0);
fclose(f);

if (!x)
{
    return 0;
}

return PICA_id_from_X509(x, id);
}

int PICA_get_id_from_cert_string(const char *cert_pem, unsigned char *id)
{
X509 *x;
BIO *biomem;

biomem = BIO_new_mem_buf(cert_pem, -1);

x = PEM_read_bio_X509(biomem, NULL, 0, NULL);

if (!x)
{
    return 0;
}

return PICA_id_from_X509(x, id);
}

static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{ //return 1 for self-signed certificates
char    buf[256];
X509   *err_cert;
int     err, depth;
SSL    *ssl;

err_cert = X509_STORE_CTX_get_current_cert(ctx);
err = X509_STORE_CTX_get_error(ctx);
depth = X509_STORE_CTX_get_error_depth(ctx);


X509_NAME_oneline(X509_get_subject_name(err_cert), buf, 256);

if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT && depth == 0)
    preverify_ok = 1;

if (!preverify_ok) {
               printf("verify error:num=%d:%s:depth=%d:%s\n", err,
                        X509_verify_cert_error_string(err), depth, buf);
           }


           /*
            * At this point, err contains the last verification error. We can use
            * it for something special
            */
           if (!preverify_ok && (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT))
           {
             X509_NAME_oneline(X509_get_issuer_name(ctx->current_cert), buf, 256);
             printf("issuer= %s\n", buf);
           }



    return preverify_ok;
}

static int add_chaninfo(struct PICA_conninfo *ci, struct PICA_chaninfo **chn, const unsigned char *peer_id, int is_outgoing)
{
struct PICA_chaninfo *chnl,*ipt;

chnl=*chn=(struct PICA_chaninfo*)calloc(sizeof(struct PICA_chaninfo),1);

if (!chnl)
	return 0;//ERR_CHECK

chnl->conn = ci;
chnl->outgoing = is_outgoing;
memcpy(chnl->peer_id, peer_id, PICA_ID_SIZE);

chnl->timestamp = time(0);

chnl->sck_data = -1;//prevent closing stdin when closing non-active connection

chnl->read_buf = calloc(PICA_CHANREADBUFSIZE, 1);

if (ci->chan_list_head)
	{
	ci->chan_list_end->next=chnl;
	chnl->prev=ci->chan_list_end;
	ci->chan_list_end=chnl;
	}
else
	{
	ci->chan_list_head=chnl;
	ci->chan_list_end=chnl;
	}

return 1;
}

static int establish_data_connection(struct PICA_chaninfo *chnl)
{
char* DN_str;
//unsigned char buf[12];
int ret,err_ret;
unsigned int tmp_uid;
struct PICA_proto_msg *mp;

chnl->sck_data=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

{
struct sockaddr_in addr=chnl->conn->srv_addr;

addr.sin_port = htons(ntohs(addr.sin_port));

ret=connect( chnl->sck_data, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));

if (ret==SOCKET_ERROR)
	{
	err_ret=PICA_ERRSOCK;
	goto error_ret;
	}
}

mp = c2c_writebuf_push( chnl, PICA_PROTO_CONNID, PICA_PROTO_CONNID_SIZE);
if (mp)
	{
	if (chnl->outgoing)
		{
		memcpy(mp->tail, chnl->conn->id, PICA_ID_SIZE);//вызвывающий
		memcpy(mp->tail + PICA_ID_SIZE, chnl->peer_id, PICA_ID_SIZE);//вызываемый
		}
	else
		{
		memcpy(mp->tail, chnl->peer_id, PICA_ID_SIZE);//вызвывающий
		memcpy(mp->tail + PICA_ID_SIZE, chnl->conn->id, PICA_ID_SIZE);//вызываемый
		}    
	}
else
	{
	err_ret=PICA_ERRNOMEM;
	goto error_ret;
	}

do
	{
	 err_ret = PICA_write_c2c(chnl);
	 if (PICA_OK != err_ret)
		goto error_ret;
	}
while( chnl->write_pos > 0);

chnl->ssl=SSL_new(chnl->conn->ctx);

if (!chnl->ssl)
	{
	//puts("SSL_new !-  0");//debug
	err_ret=PICA_ERRSSL;
	goto error_ret;
	}

ret=SSL_set_fd(chnl->ssl, chnl->sck_data);

if (!ret)
	{
	//puts("SSL_set_fd returned 0");//debug
	err_ret=PICA_ERRSSL;
	goto error_ret_;
	}

SSL_set_verify(chnl->ssl, SSL_VERIFY_PEER, verify_callback);
SSL_set_verify_depth(chnl->ssl, 1);//peer certificate and one CA


if (chnl->outgoing)
ret=SSL_connect(chnl->ssl);
else
ret=SSL_accept(chnl->ssl);

//printf("verify result:%i\n",(int)SSL_get_verify_result(chnl->ssl));//debug

if (ret!=1)
	{
    //printf("ret=%i of SSL_connect or accept\n",ret);//debug
    //printf("SSL_get_error says:%i\n",SSL_get_error(chnl->ssl,ret));//debug
	err_ret=PICA_ERRSSL;
	goto error_ret_;
	}

//проверить сертификат собеседника

chnl->peer_cert=SSL_get_peer_certificate(chnl->ssl);

if (!chnl->peer_cert)
	{
	err_ret=PICA_ERRNOPEERCERT;
	goto error_ret_;
	}

{
unsigned char temp_id[PICA_ID_SIZE];

if (PICA_id_from_X509(chnl->peer_cert, temp_id) == 0)
	{
	err_ret=PICA_ERRNOPEERCERT;
	goto error_ret_;
	}

if (memcmp(temp_id, chnl->peer_id, PICA_ID_SIZE) != 0)
	{
	err_ret=PICA_ERRINVPEERCERT;
	goto error_ret_;
	}
}
/*
DN_str=X509_NAME_oneline(X509_get_subject_name(chnl->peer_cert), 0, 0);

if (!DN_str)
	{
	//puts("no DN_str");//debug
	err_ret=PICA_ERRSSL;
	goto error_ret_;
	}

if (!get_id_fromsubjstr(DN_str,&tmp_uid)  || tmp_uid!=chnl->peer_id)
	{
	err_ret=PICA_ERRINVPEERCERT;
	goto error_ret_;
	}
//puts(DN_str);//debug

OPENSSL_free(DN_str);*/



{
BIO *mem = BIO_new(BIO_s_mem());
char *cert_pem;
unsigned int cert_pem_nb;

PEM_write_bio_X509(mem, chnl->peer_cert);

cert_pem_nb =  (unsigned int)BIO_get_mem_data(mem, &cert_pem);

if (callbacks.peer_cert_verify_cb(chnl->peer_id, (const char*)cert_pem, cert_pem_nb) == 0)
	{
	BIO_free(mem);
	err_ret=PICA_ERRINVPEERCERT;
	goto error_ret_;
	}

BIO_free(mem);
}

chnl->state=PICA_CHANSTATE_ACTIVE;

return PICA_OK;

error_ret_:
error_ret:
callbacks.channel_failed(chnl->peer_id);
PICA_close_channel(chnl);
return err_ret;
}

static unsigned int procmsg_INITRESP(unsigned char* buf, unsigned int nb,void* p)
{
struct PICA_conninfo *ci=(struct PICA_conninfo *)p;

switch(buf[0])
	{
	case PICA_PROTO_INITRESPOK:
	ci -> init_resp_ok = 1;
	break;
	
	case PICA_PROTO_VERDIFFER:
	ci->init_resp_ok = -1;
	ci->node_ver_major = buf[2];
	ci->node_ver_minor = buf[3];
	break;
	
	default:
	return 0;
	}    

return 1;
}

static unsigned int procmsg_CONNREQINC(unsigned char* buf,unsigned int nb,void* p)
{
int ret;
struct PICA_conninfo *ci=(struct PICA_conninfo *)p;
struct PICA_proto_msg *mp;
unsigned char *peer_id = buf + 2;


	if (callbacks.accept_cb(peer_id))
		{
		struct PICA_chaninfo *_chnl;
		
		mp = c2n_writebuf_push( ci, PICA_PROTO_CONNALLOW, PICA_PROTO_CONNALLOW_SIZE);
		
		if (mp)
			{
			memcpy(mp->tail, peer_id, PICA_ID_SIZE);
			}
		else
			return 0;
		
		ret = add_chaninfo( ci, &_chnl, peer_id, 0);

		if (!ret)
			return 0; //ERR_CHECK - кончилась память
			
		do
			{
			ret = PICA_write_c2n(ci);
			if (PICA_OK != ret)
				return 0;
			}
		while( ci->write_pos > 0);

		ret = establish_data_connection(_chnl);

		if (ret!=PICA_OK)
			return 1;

		callbacks.channel_established_cb(_chnl->peer_id);//*nchn=_chnl;

		return 1;
		}
	else
		{
		mp = c2n_writebuf_push(ci, PICA_PROTO_CONNDENY, PICA_PROTO_CONNDENY_SIZE);
	
		if (mp)
			{
			memcpy(mp->tail, peer_id, PICA_ID_SIZE);
			}
		else
			return 0;//ERR_CHECK
		}
return 1;
}

static unsigned int procmsg_NOTFOUND(unsigned char* buf,unsigned int nb,void* p)
{
struct PICA_conninfo *ci=(struct PICA_conninfo *)p;
struct PICA_chaninfo *ipt,*rq=0;
unsigned char *peer_id = buf + 2;

//puts("procmsg_NOTFOUND");//debug

ipt=ci->chan_list_end;
while(ipt)
	{
	if (memcmp(ipt->peer_id, peer_id, PICA_ID_SIZE) == 0)
		{
		rq=ipt;
		break;
		}
	ipt=ipt->prev;
	}

if (!rq)
	return 0;// ERR_CHECK -левое сообщение, такой запрос не посылался

//puts("procmsg_NOTFOUND_chkp0");//debug
PICA_close_channel(rq);

//puts("procmsg_NOTFOUND_chkp1");//debug

callbacks.notfound_cb(peer_id);
return 1;
}

static unsigned int procmsg_FOUND(unsigned char* buf,unsigned int nb,void* p)
{
struct PICA_conninfo *ci=(struct PICA_conninfo *)p;
struct PICA_chaninfo *ipt,*rq=0;
unsigned char *peer_id = buf + 2;
int ret;

ipt=ci->chan_list_end;
while(ipt)
	{
	if (memcmp(ipt->peer_id, peer_id, PICA_ID_SIZE) == 0)
		{
		rq=ipt;
		break;
		}
	ipt=ipt->prev;
	}		

if (!rq)
	return 0;// ERR_CHECK -левое сообщение, такой запрос не посылался

ret=establish_data_connection(rq);

if (ret!=PICA_OK)
	return 0;

callbacks.channel_established_cb(rq->peer_id);//*nchn=_chnl;

return 1;
}

static unsigned int procmsg_CLNODELIST(unsigned char* buf, unsigned int nb, void* p)
{
unsigned int pos = 4;
char ipaddr_string[INET6_ADDRSTRLEN];
char temp_zeroswap;
unsigned int port;

//puts("CLNODELIST");//debug

//callbacks.nodelist_cb(buf + 4, nb - 4);
do
	{
	switch(buf[pos])
		{
	    	case PICA_PROTO_NEWNODE_IPV4:
		    
		callbacks.nodelist_cb(
		    		PICA_PROTO_NEWNODE_IPV4, 
				buf + pos +1, 
				inet_ntop(AF_INET, buf + pos +1, ipaddr_string, INET6_ADDRSTRLEN), 
				ntohs(*(uint16_t*)(buf + pos + 5))
			  );
		
        pos += PICA_PROTO_NODELIST_ITEM_IPV4_SIZE;
		break;
		
	    	case PICA_PROTO_NEWNODE_IPV6:
		  
		callbacks.nodelist_cb(
		    		PICA_PROTO_NEWNODE_IPV6, 
				buf + pos +1, 
				inet_ntop(AF_INET6, buf + pos +1, ipaddr_string, INET6_ADDRSTRLEN), 
				ntohs(*(uint16_t*)(buf + pos + 17))
			  );
		  
        pos += PICA_PROTO_NODELIST_ITEM_IPV6_SIZE;
		break;
		
	    	case PICA_PROTO_NEWNODE_DNS:
		
		port = ntohs(*(uint16_t*)(buf + pos + buf[pos+1] + 2));
		
		temp_zeroswap = buf[pos + 2 + buf[pos+1]]; //save byte after hostname string
		buf[pos + 2 + buf[pos+1]] = '\0';// and replace it with string terminating zero
		
		callbacks.nodelist_cb(
		    		PICA_PROTO_NEWNODE_DNS, 
				buf + pos +2, 
				buf + pos +2, 
				port
			  );
		 
		buf[pos + 2 + buf[pos+1]] = temp_zeroswap;
		
		pos += 4 + buf[pos+1];
		break;
		
	    	default:
		//puts("unknown node address type");//debug
		return 0;
		}
	}
while(pos < nb);

return 1;
}

static unsigned int procmsg_MSGUTF8(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_chaninfo *chan = (struct PICA_chaninfo *)p;
struct PICA_proto_msg *mp;

callbacks.newmsg_cb(chan->peer_id, buf + 4, nb - 4, buf[0]);

//send PICA_PROTO_MSGOK
mp = c2c_writebuf_push( chan, PICA_PROTO_MSGOK, PICA_PROTO_MSGOK_SIZE);

if (mp)
	{
	RAND_bytes( mp->tail, 2);
	}
else
	return 0;

return 1;
}

static unsigned int procmsg_MSGOK(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_chaninfo *chan = (struct PICA_chaninfo *)p;

callbacks.msgok_cb(chan -> peer_id);
return 1;
}

unsigned int procmsg_PINGREQ(unsigned char* buf,unsigned int nb,void* p)
{
struct PICA_conninfo *ci=(struct PICA_conninfo *)p;
struct PICA_proto_msg *mp;

mp = c2n_writebuf_push(ci, PICA_PROTO_PINGREP, PICA_PROTO_PINGREP_SIZE);

if (mp)
    {
    RAND_bytes(mp->tail, 2);
    }
else
    return 0;

return 1;
}

#ifdef PICA_MULTITHREADED

#ifdef WIN32
void locking_cb(int mode, int type, char *file, int line)
{
    if (mode & CRYPTO_LOCK)
        WaitForSingleObject(mt_locks[type],INFINITE);
    else
        ReleaseMutex(mt_locks[type]);
}
#else
unsigned long thread_id_cb(void)
{
    return (unsigned long)pthread_self();
}

void locking_cb(int mode, int type, char *file, int line)
{
    if (mode & CRYPTO_LOCK)
        pthread_mutex_lock(&mt_locks[type]);
    else
        pthread_mutex_unlock(&mt_locks[type]);
}
#endif

#endif


int PICA_client_init(struct PICA_client_callbacks *clcbs)
{
struct sockaddr_in sd;
#ifdef WIN32
WSADATA wsd;
WSAStartup(MAKEWORD(2,2),&wsd);
#endif

if (!clcbs)
	return 0;

callbacks=*clcbs;

SSL_load_error_strings();
SSL_library_init(); 



#ifdef NO_RAND_DEV  
PICA_rand_seed();
#endif

#ifdef PICA_MULTITHREADED
{
    int i;
#ifdef WIN32
mt_locks = malloc(CRYPTO_num_locks()*sizeof(HANDLE));

for (i=0; i<CRYPTO_num_locks(); i++)
    mt_locks[i] = CreateMutex(NULL,FALSE,NULL);

CRYPTO_set_locking_callback(locking_cb);
#else
mt_locks = malloc(CRYPTO_num_locks()*sizeof(pthread_mutex_t));

memset(mt_locks,0,CRYPTO_num_locks()*sizeof(pthread_mutex_t));

for (i=0; i<CRYPTO_num_locks(); i++)
    pthread_mutex_init(&mt_locks[i],NULL);

CRYPTO_set_id_callback(thread_id_cb);
CRYPTO_set_locking_callback(locking_cb);
#endif
}
#endif

//ctx=SSL_CTX_new(TLSv1_method());
//if (!ctx)
//;//ERR_CHECK

#ifndef WIN32
signal(SIGPIPE, SIG_IGN);
#endif

return 1;
}

/*адрес сервера, сертификат клиента и закрытый ключ*/

//Осуществляет подключение к серверу по указанному адресу.
//Аргументы:
//nodeaddr - адрес узла
//port - номер порта, в нативном для хост-машины порядке байт
//CA_file - имя файла, содержащего корневой сертификат в формате PEM
//cert_file - имя файла, содержащего сертфикат клиента в формате PEM
//pkey_file - имя файла, содержащего приватный ключ в формате PEM
//password_cb - callback-функция, запрашивающая пароль к приватному ключу, см. man 3 SSL_CTX_set_default_passwd_cb;в userdata передается указатель на id
//ci -указатель на указатель на структуру PICA_conninfo, который будет проинициализирован при успешном выполнении функции
//Память, выделенная под структуру, освобождается при вызове PICA_close_connection
//Возвращаемое значение - код завершения
//PICA_OK - успешное завершение функции
//....

int PICA_new_connection(const char *nodeaddr,
                        unsigned int port,
                        const char *CA_file,
                        const char *cert_file,
                        const char *pkey_file,
                        const char *dh_param_file,
                        int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
                        struct PICA_conninfo **ci)
{
int ret,ret_err;
struct PICA_conninfo *cid;
struct sockaddr_in a;
DH *dh = NULL;
FILE *dh_file = NULL;


if (!(nodeaddr && CA_file && cert_file && pkey_file && ci))
	return PICA_ERRINVARG;


cid=*ci=(struct PICA_conninfo*)calloc(sizeof(struct PICA_conninfo),1);//(1)
if (!cid)
	return PICA_ERRNOMEM;

cid->read_buf = calloc(PICA_CONNREADBUFSIZE, 1);//(2)
if (!cid->read_buf)
    {
    ret_err = PICA_ERRNOMEM;
    goto error_ret_1;
    }

cid->ctx=SSL_CTX_new(TLSv1_2_method());//(3)

if (!cid->ctx)
	{
	ret_err=PICA_ERRSSL;
    goto error_ret_2;
	}

dh_file = fopen(dh_param_file, "r");

if (dh_file)
    {
    dh = PEM_read_DHparams(dh_file, NULL, NULL, NULL);
    fclose(dh_file);
    }

if (1 != SSL_CTX_set_tmp_dh(cid->ctx, dh))
{
    ret_err = PICA_ERRSSL;
    goto error_ret_2;
}

DH_free (dh);

if (!PICA_get_id_from_cert_file(cert_file, cid->id))
    {
    ret_err=PICA_ERRINVCERT;
    goto error_ret_3;
    }
////<<
if (password_cb)
    {
    SSL_CTX_set_default_passwd_cb(cid->ctx, password_cb);
    SSL_CTX_set_default_passwd_cb_userdata(cid->ctx,cid->id);
    }

ret=SSL_CTX_use_certificate_file(cid->ctx,cert_file,SSL_FILETYPE_PEM);
if (ret!=1)
	{//ERR_CHECK
	ret_err=PICA_ERRSSL;
    goto error_ret_3;
	}
ret=SSL_CTX_use_PrivateKey_file(cid->ctx,pkey_file,SSL_FILETYPE_PEM);
if (ret!=1)
       {//ERR_CHECK
       ret_err=PICA_ERRSSL;
       goto error_ret_3;
       }
////<<

ret=SSL_CTX_load_verify_locations(cid->ctx,CA_file,0/*"trustedCA/"*/);
//printf("loadverifylocations ret=%i\n",ret);//debug
SSL_CTX_set_client_CA_list(cid->ctx,SSL_load_client_CA_file(CA_file));

ret = SSL_CTX_set_cipher_list(cid->ctx,"DHE-RSA-CAMELLIA256-SHA"/*:DHE-RSA-AES256-SHA256"*/);

if (ret != 1)
{
    ret_err = PICA_ERRSSL;
    goto error_ret_2;
}


cid->ssl_comm=SSL_new(cid->ctx);//(4)

if (!cid->ssl_comm)
	{
	ret_err=PICA_ERRSSL;
    goto error_ret_3;
	}


//resolving name
{
    struct addrinfo h,*r;

    memset(&h,0,sizeof(struct addrinfo));

    h.ai_flags=0;
#ifdef AI_IDN
    h.ai_flags = h.ai_flags | AI_IDN;
#endif
#ifdef AI_ADDRCONFIG
    h.ai_flags = h.ai_flags | AI_ADDRCONFIG;
#endif
    h.ai_family=AF_INET;//CONF
    h.ai_socktype=SOCK_STREAM;
    h.ai_protocol=IPPROTO_TCP;

    if (0!=getaddrinfo(nodeaddr,NULL,&h,&r))
        {
        ret_err=PICA_ERRDNSRESOLVE;
        goto error_ret_4;
        }

    memset(&a,0,sizeof(a));
    memcpy(&a,r->ai_addr,r->ai_addrlen);
    a.sin_family      = AF_INET;
    //a.sin_addr.s_addr = inet_addr (nodeaddr);
    a.sin_port        = htons(port);

    freeaddrinfo(r);
}

cid->srv_addr=a;

cid->sck_comm=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);//(5)
if (cid->sck_comm==SOCKET_ERROR)
	{
	ret_err=PICA_ERRSOCK;
    goto error_ret_4;
	}

cid->state = PICA_CONNSTATE_CONNECTING;

ret=connect(cid->sck_comm,(struct sockaddr*)&a,sizeof(struct sockaddr_in));


if (ret==SOCKET_ERROR)
	{
	ret_err=PICA_ERRSOCK;
    goto error_ret_5;
	}


{
struct PICA_proto_msg *mp;

mp = c2n_writebuf_push( cid, PICA_PROTO_INITREQ, PICA_PROTO_INITREQ_SIZE);
	
if (mp)
	{
	mp->tail[0] = PICA_PROTO_VER_HIGH;
	mp->tail[1] = PICA_PROTO_VER_LOW;
	}
else
	{
	ret_err = PICA_ERRNOMEM;
    goto error_ret_5;
	}
}

do
	{
	ret = PICA_write_c2n(cid);
	if (ret != PICA_OK)
		{
		ret_err = ret;
		break;
		}
	ret = PICA_read_c2n(cid);
	if (ret != PICA_OK)
		{
		ret_err = ret;
		break;
		}
	//TODO Timeout
	}
while (cid->init_resp_ok == 0);

if (cid->init_resp_ok != 1)
	{
	if (cid->init_resp_ok == -1)
        {
        if (cid->node_ver_major > PICA_PROTO_VER_HIGH ||
                (cid->node_ver_major == PICA_PROTO_VER_HIGH && cid->node_ver_minor > PICA_PROTO_VER_LOW))
            ret_err = PICA_ERRPROTONEW;
        else
            ret_err = PICA_ERRPROTOOLD;
        }
    goto error_ret_5;
	}

//printf("6\n");//debug

ret=SSL_set_fd(cid->ssl_comm,cid->sck_comm);

//printf("SSL_set_fd=%i\n",ret);

ret=SSL_accept(cid->ssl_comm);

if (ret!=1)
	{
	//printf("SSL_accept  ret=%i\n  SSL_get_error=%i\n",ret,SSL_get_error(cid->ssl_comm,ret));//debug
		//ERR_CHECK
	ret_err=PICA_ERRSSL;
    goto error_ret_5;
	}
cid->state = PICA_CONNSTATE_CONNECTED;//<<<!!!

{
struct PICA_proto_msg *mp;

mp = c2n_writebuf_push( cid, PICA_PROTO_CLNODELISTREQ, PICA_PROTO_CLNODELISTREQ_SIZE);
	
if (mp)
	{
	RAND_bytes(mp->tail, 2);
	}    
}

return PICA_OK;


error_ret_5: //(5)
    SHUTDOWN(cid->sck_comm);
    CLOSE(cid->sck_comm);

error_ret_4: //(4)
    SSL_free(cid->ssl_comm);

error_ret_3: //(3)
    SSL_CTX_free(cid->ctx);

error_ret_2: //(2)
    free(cid->read_buf);

error_ret_1: //(1)
    free(cid);
    *ci=0;

return ret_err;
}



//создает ИСХОДЯЩИЙ логический зашифрованный канал связи с указанным собеседником, если тот доступен
// в данный момент.
int PICA_create_channel(struct PICA_conninfo *ci,const unsigned char *peer_id,struct PICA_chaninfo **chn)
{
struct PICA_proto_msg *mp;
struct PICA_chaninfo *chnl;

if (!(ci && chn))
	return PICA_ERRINVARG;

if (!add_chaninfo(ci, chn, peer_id, PICA_CHANNEL_OUTGOING))
	return PICA_ERRNOMEM;

chnl=*chn;

mp = c2n_writebuf_push( ci, PICA_PROTO_CONNREQOUTG, PICA_PROTO_CONNREQOUTG_SIZE);
		
	if (mp)
		{
		memcpy(mp->tail, peer_id, PICA_ID_SIZE);
		}
	else
		 return PICA_ERRNOMEM;


return PICA_OK;
}

int PICA_read(struct PICA_conninfo *ci,int timeout)
{
fd_set fds;
struct timeval tv={0,1000};
struct PICA_chaninfo *ipt, *kill_ptr = 0;
int ret,nfds;

//puts("PICA_read<<");//debug

tv.tv_usec=timeout;

FD_ZERO(&fds);

nfds=ci->sck_comm;
FD_SET(ci->sck_comm,&fds);

ipt=ci->chan_list_head;

while(ipt)
	{
	if (ipt->state==PICA_CHANSTATE_ACTIVE)
		{
		FD_SET(ipt->sck_data,&fds);

		if (ipt->sck_data>nfds)
			nfds=ipt->sck_data;
		}	

	if (ipt->state != PICA_CHANSTATE_ACTIVE && (time(0) - ipt->timestamp) > PICA_CHAN_ACTIVATE_TIMEOUT)
		{
		kill_ptr = ipt;  
		}

	ipt=ipt->next;

	if (kill_ptr)
		{
		PICA_close_channel(kill_ptr);
		kill_ptr = 0;
		}
	}

//#warning "make sockets non-blocking!!!!"

ret=select(nfds+1,&fds,0,0,&tv);

if (ret>0)
	{
	
	if (FD_ISSET(ci->sck_comm,&fds))
		{
		ret=PICA_read_c2n(ci);
		if (ret!=PICA_OK)
			return ret;
		}

	ipt=ci->chan_list_head;
	while(ipt)
		{
		if (ipt->state==PICA_CHANSTATE_ACTIVE && FD_ISSET(ipt->sck_data, &fds))
			{
			if (PICA_OK != (ret = PICA_read_c2c(ipt)) )
				{
				callbacks.channel_closed_cb(ipt->peer_id, ret);
				kill_ptr = ipt;
				}
			    
			}
		
        ipt=ipt->next;

		if (kill_ptr)
			{
			PICA_close_channel(kill_ptr);
			kill_ptr = 0;
			}
		}

	}
//puts("PICA_read>>");//debug

return PICA_OK;
}

int PICA_write(struct PICA_conninfo *ci)
{
struct PICA_chaninfo *ipt, *kill_ptr = 0;
int ret = PICA_OK;

//puts("PICA_write>>");//debug

if (ci->write_pos)
	ret = PICA_write_c2n(ci);

if (PICA_OK != ret)
	return ret;

ipt=ci->chan_list_head;

while(ipt)
	{
	if (ipt->write_pos)
		{
		if (PICA_OK != (ret = PICA_write_c2c(ipt)))
			{
			callbacks.channel_closed_cb(ipt->peer_id, ret);
			kill_ptr = ipt;
			}
		}
	ipt = ipt->next;

	if (kill_ptr)
		{
		PICA_close_channel(kill_ptr);
		kill_ptr = 0;
		}
	}

//puts("PICA_write<<");//debug
return PICA_OK;
}

int PICA_write_socket(SOCKET s, unsigned char *buf, unsigned int *ppos)
{
int ret;

if (*ppos > 0 )
	{
	ret = send( s, buf, *ppos, MSG_NOSIGNAL);

	if (ret > 0)
		{
		if (ret < *ppos)
			memmove( buf, buf + ret, *ppos - ret);
		*ppos -= ret;
		}
		
	if (ret < 0)
		{
#ifdef WIN32
		ret = WSAGetLastError();
		if (! (ret == WSAEWOULDBLOCK || ret == WSAENOBUFS))
#else
		ret = errno;
		if (! (ret == EAGAIN || ret == ENOBUFS || ret == EINTR))
#endif
			return PICA_ERRSOCK;
		}
	if (ret == 0)
		return PICA_ERRSERV;
		
	}
return PICA_OK;
}

int PICA_write_ssl(SSL *ssl, unsigned char *buf, unsigned int *ppos)
{
int ret;

if (*ppos > 0 )
	{
	ret = SSL_write( ssl, buf, *ppos);
	
	if (ret > 0)
		{
		if (ret < *ppos)
			memmove( buf, buf + ret, *ppos - ret);
		*ppos -= ret;
		}
		
	if (ret < 0)
		{
		ret = SSL_get_error( ssl, ret);
		if (! (ret == SSL_ERROR_WANT_WRITE || ret == SSL_ERROR_WANT_READ))
				return PICA_ERRSSL;
		}
	if (ret == 0)
		return PICA_ERRSERV;
		
	}
return PICA_OK;
}

//int PICA_read_socket()
//int PICA_read_ssl()

int PICA_write_c2n(struct PICA_conninfo *ci)
{
int ret = PICA_ERRINVARG;

if (PICA_CONNSTATE_CONNECTING == ci->state)
	ret = PICA_write_socket( ci->sck_comm, ci->write_buf, &ci->write_pos);
else if (PICA_CONNSTATE_CONNECTED == ci->state)
	ret = PICA_write_ssl( ci->ssl_comm, ci->write_buf, &ci->write_pos);

return ret;
}

int PICA_write_c2c(struct PICA_chaninfo *chn)
{
int ret = PICA_ERRINVARG;

if (PICA_CHANSTATE_ACTIVE != chn->state)
	ret = PICA_write_socket( chn->sck_data, chn->write_buf, &chn->write_pos);
else
	ret = PICA_write_ssl( chn->ssl, chn->write_buf, &chn->write_pos);

return ret;
}

int PICA_read_socket(SOCKET s, unsigned char *buf, unsigned int *ppos, unsigned int size)
{
int ret;

ret = recv(s, buf + *ppos, size - *ppos, 0);

if (ret == 0)
	return PICA_ERRDISCONNECT;

if (ret < 0)
	{
#ifdef WIN32
	switch(WSAGetLastError())
		{
		case WSAEINTR:
		case WSAEINPROGRESS:
		case WSAEWOULDBLOCK:
			break;
		default:
			return PICA_ERRSOCK;
		}
#else
	switch(errno)
		{
		case EAGAIN :
#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
#endif
		case EINTR:
		break;
		
		default:
			return PICA_ERRSOCK;//ERR_CHECK
		}
#endif
	}
	
if (ret > 0)
	{
	*ppos += ret;
	}
	
return PICA_OK;
}

int PICA_read_ssl(SSL *ssl, unsigned char *buf, unsigned int *ppos, unsigned int size)
{
int ret;

ret = SSL_read(ssl, buf + *ppos, size - *ppos);

if (!ret)
 	return PICA_ERRSSL;

if (ret < 0)
	switch(SSL_get_error(ssl, ret))
		{
		case SSL_ERROR_WANT_WRITE:
		case SSL_ERROR_WANT_READ:
		break;
		default:
		return PICA_ERRSSL;
		};
	
if (ret > 0)
	{
	*ppos += ret;
	}
	
return PICA_OK;    
}

int PICA_read_c2c(struct PICA_chaninfo *chn)
{
int ret;

if (chn->state != PICA_CHANSTATE_ACTIVE)
	return PICA_OK;// ??? 

//ret = SSL_read(chn->ssl , chn->read_buf + chn->read_pos, PICA_CHANREADBUFSIZE - chn->read_pos);
ret = PICA_read_ssl(chn->ssl, chn->read_buf, &chn->read_pos, PICA_CHANREADBUFSIZE);
		
if (ret == PICA_OK)
	{
	if(!PICA_processdatastream(chn->read_buf, &(chn->read_pos ), chn, c2c_messages, MSGINFO_MSGSNUM(c2c_messages)))
		return PICA_ERRSERV;
	}

return ret;
}

//функция читает и обрабатывает данные, приходящие от сервера по управляющему соединению.
int PICA_read_c2n(struct PICA_conninfo *ci) 
{
int ret;

//ret=SSL_read(ci->ssl_comm, ci->read_buf + ci->read_pos, PICA_CONNREADBUFSIZE - ci->read_pos);
if (PICA_CONNSTATE_CONNECTING == ci->state)
	ret = PICA_read_socket(ci->sck_comm, ci->read_buf, &ci->read_pos, PICA_CONNREADBUFSIZE);
else if (PICA_CONNSTATE_CONNECTED == ci->state)
	ret = PICA_read_ssl(ci->ssl_comm,  ci->read_buf, &ci->read_pos, PICA_CONNREADBUFSIZE);



if (ret == PICA_OK)
	{
	if (PICA_CONNSTATE_CONNECTING == ci->state)
		if(!PICA_processdatastream( ci->read_buf, &(ci->read_pos), ci, c2n_init_messages, MSGINFO_MSGSNUM(c2n_init_messages)))
			return PICA_ERRSERV;
	
	if (PICA_CONNSTATE_CONNECTED == ci->state)
		if(!PICA_processdatastream( ci->read_buf, &(ci->read_pos), ci, c2n_messages, MSGINFO_MSGSNUM(c2n_messages)))
			return PICA_ERRSERV;
	}

return ret;
} 

struct PICA_proto_msg* c2n_writebuf_push(struct PICA_conninfo *ci, unsigned int msgid, unsigned int size)
{
struct PICA_proto_msg *mp;
    
if (! ci->write_buf)
	{
	ci->write_buf = calloc(PICA_CONNWRITEBUFSIZE, 1);
	if (!ci->write_buf)
		return 0;
	ci->write_buflen = PICA_CONNWRITEBUFSIZE;
	}
	
if (ci->write_buflen - ci->write_pos < size)
	{
	unsigned char*p;
	p = realloc(ci->write_buf, size + ci->write_pos);
	
	if (p)
		{
		ci->write_buf = p;
		ci->write_buflen = size + ci->write_pos;
		}
	else
		return 0;
	}
	
mp = (struct PICA_proto_msg *)(ci->write_buf + ci->write_pos);
mp->head[0] = mp->head[1] = msgid;
ci->write_pos += size;

return mp;
}

struct PICA_proto_msg* c2c_writebuf_push(struct PICA_chaninfo *chn, unsigned int msgid, unsigned int size)
{
struct PICA_proto_msg *mp;
    
if (! chn->write_buf)
	{
	chn->write_buf = calloc(PICA_CHANWRITEBUFSIZE, 1);
	if (!chn->write_buf)
		return 0;
	chn->write_buflen = PICA_CHANWRITEBUFSIZE;
	}
	
if (chn->write_buflen - chn->write_pos < size)
	{
	unsigned char*p;
	p = realloc(chn->write_buf, size + chn->write_pos);
	
	if (p)
		{
		chn->write_buf = p;
		chn->write_buflen = size + chn->write_pos;
		}
	else
		return 0;
	}
	
mp = (struct PICA_proto_msg *)(chn->write_buf + chn->write_pos);
mp->head[0] = mp->head[1] = msgid;
chn->write_pos += size;

return mp;
}

int PICA_send_msg(struct PICA_chaninfo *chn, char *buf,unsigned int len)
{
struct PICA_proto_msg *mp;

if (len > PICA_PROTO_C2CMSG_MAXDATASIZE)
	return PICA_ERRMSGSIZE;

if ((mp = c2c_writebuf_push(chn, PICA_PROTO_MSGUTF8, len + 4)))
	{
	*((uint16_t*)mp->tail) = len;
	memcpy(mp->tail + 2, buf, len);
	}
else
	return PICA_ERRNOMEM;

return PICA_OK;
}

void PICA_close_channel(struct PICA_chaninfo *chn)
{
struct PICA_chaninfo *ipt;

//puts("PICA_close_channel");//debug
	
ipt=chn->conn->chan_list_head;
//puts("PICA_close_channel_chkp0");//debug
while(ipt)
	{
	if (ipt==chn)
		{
		if (ipt->next)
			ipt->next->prev=ipt->prev;
		else
			chn->conn->chan_list_end=ipt->prev;

		if (ipt->prev)
			ipt->prev->next=ipt->next;
		else
			chn->conn->chan_list_head=ipt->next;

		break;			
		}
	ipt=ipt->next;
	}

//puts("PICA_close_channel_chkp1");//debug

if (chn -> read_buf)
	free(chn -> read_buf);

if (chn -> write_buf)
	free(chn -> write_buf);

if (chn->peer_cert)
	X509_free(chn->peer_cert);

//puts("PICA_close_channel_chkp2");//debug

if (chn->ssl)
	SSL_shutdown(chn->ssl);
	//puts("PICA_close_channel_chkp3");//debug

if (chn->ssl)
	{
	//printf("3 SSL_free(%X)\n",chn->ssl);//debug
	SSL_free(chn->ssl);
	}

//puts("PICA_close_channel_chkp4");//debug
SHUTDOWN(chn->sck_data);
//puts("PICA_close_channel_chkp5");//debug
CLOSE(chn->sck_data);
//puts("PICA_close_channel_chkp6");//debug
free(chn);
//puts("PICA_close_channel_chkp7");//debug
}

void PICA_close_connection(struct PICA_conninfo *cid)
{
struct PICA_chaninfo *ipt;

while((ipt = cid->chan_list_head))
	PICA_close_channel(ipt);

SSL_shutdown(cid->ssl_comm);
//printf("4 SSL_free(%X)\n",cid->ssl_comm);//debug
SSL_free(cid->ssl_comm);
SHUTDOWN(cid->sck_comm);
CLOSE(cid->sck_comm);
SSL_CTX_free(cid->ctx);
free(cid->read_buf);

if (cid->write_buf)
	free(cid->write_buf);

free(cid);
}

#ifdef WIN32
 const char *PICA_inet_ntop(int af, const void *src, char *dst, size_t size)
{
    unsigned char *chsrc = (unsigned char*)src;
    switch(af)
    {
    case AF_INET:
    sprintf(dst, "%u.%u.%u.%u", chsrc[0], chsrc[1], chsrc[2], chsrc[3]);
    break;

    case AF_INET6:
    sprintf(dst, "%x%02x:%x%02x:%x%02x:%x%02x:%x%02x:%x%02x:%x%02x:%x%02x", chsrc[0], chsrc[1], chsrc[2], chsrc[3],
                                                            chsrc[4], chsrc[5], chsrc[6], chsrc[7],
                                                            chsrc[8], chsrc[9], chsrc[10], chsrc[11],
                                                            chsrc[12],chsrc[13], chsrc[14], chsrc[15]);
    break;

    default:
    return NULL;
    }

    return dst;
}
#endif
