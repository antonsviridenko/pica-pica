#include "PICA_client.h"
#include "PICA_proto.h"
#include "PICA_msgproc.h"
#include "PICA_common.h"
#include <openssl/dh.h>
#include <string.h>

#define PICA_DEBUG // debug

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

static int PICA_send_file_fragment(struct PICA_c2c *chn);
static int PICA_sendfile_open_read(struct PICA_c2c *chn, const char *filename_utf8, uint64_t *file_size);
static int PICA_recvfile_open_write(struct PICA_c2c *chn, const char *filename_utf8, unsigned int filenamesize);
static int PICA_send_filecontrol(struct PICA_c2c *chan, int senderctl, int receiverctl);

static unsigned int procmsg_INITRESP(unsigned char*,unsigned int,void*);
static unsigned int procmsg_CONNREQINC(unsigned char*,unsigned int,void*);
static unsigned int procmsg_NOTFOUND(unsigned char*,unsigned int,void*);
static unsigned int procmsg_FOUND(unsigned char*,unsigned int,void*);
static unsigned int procmsg_CLNODELIST(unsigned char*,unsigned int,void*);
static unsigned int procmsg_MSGUTF8(unsigned char*,unsigned int,void*);
static unsigned int procmsg_MSGOK(unsigned char*,unsigned int,void*);
static unsigned int procmsg_PINGREQ(unsigned char*,unsigned int,void*);
static unsigned int procmsg_SENDFILEREQUEST(unsigned char*,unsigned int,void*);
static unsigned int procmsg_ACCEPTEDFILE(unsigned char*,unsigned int,void*);
static unsigned int procmsg_DENIEDFILE(unsigned char*,unsigned int,void*);
static unsigned int procmsg_FILEFRAGMENT(unsigned char*,unsigned int,void*);
static unsigned int procmsg_FILECONTROL(unsigned char*,unsigned int,void*);
static unsigned int procmsg_INITRESP_c2c(unsigned char*,unsigned int,void*);

static struct PICA_proto_msg* c2n_writebuf_push(struct PICA_c2n *ci, unsigned int msgid, unsigned int size);
static struct PICA_proto_msg* c2c_writebuf_push(struct PICA_c2c *chn, unsigned int msgid, unsigned int size);

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
	{PICA_PROTO_MSGOK, PICA_MSG_FIXED_SIZE, PICA_PROTO_MSGOK_SIZE, procmsg_MSGOK},
	{PICA_PROTO_SENDFILEREQUEST, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_SENDFILEREQUEST},
	{PICA_PROTO_ACCEPTEDFILE, PICA_MSG_FIXED_SIZE, PICA_PROTO_ACCEPTEDFILE_SIZE, procmsg_ACCEPTEDFILE},
	{PICA_PROTO_DENIEDFILE, PICA_MSG_FIXED_SIZE, PICA_PROTO_DENIEDFILE_SIZE, procmsg_DENIEDFILE},
	{PICA_PROTO_FILEFRAGMENT, PICA_MSG_VAR_SIZE, PICA_MSG_VARSIZE_INT16, procmsg_FILEFRAGMENT},
	{PICA_PROTO_FILECONTROL, PICA_MSG_FIXED_SIZE, PICA_PROTO_FILECONTROL_SIZE, procmsg_FILECONTROL}
};

struct PICA_msginfo c2n_init_messages[] = {
	{PICA_PROTO_INITRESPOK, PICA_MSG_FIXED_SIZE, PICA_PROTO_INITRESPOK_SIZE, procmsg_INITRESP},
	{PICA_PROTO_VERDIFFER, PICA_MSG_FIXED_SIZE, PICA_PROTO_VERDIFFER_SIZE, procmsg_INITRESP}
};

struct PICA_msginfo c2c_init_messages[] = {
	{PICA_PROTO_INITRESPOK, PICA_MSG_FIXED_SIZE, PICA_PROTO_INITRESPOK_SIZE, procmsg_INITRESP_c2c},
	{PICA_PROTO_VERDIFFER, PICA_MSG_FIXED_SIZE, PICA_PROTO_VERDIFFER_SIZE, procmsg_INITRESP_c2c}
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

static int add_chaninfo(struct PICA_c2n *ci, struct PICA_c2c **chn, const unsigned char *peer_id, int is_outgoing)
{
struct PICA_c2c *chnl,*ipt;

chnl=*chn=(struct PICA_c2c*)calloc(sizeof(struct PICA_c2c),1);

if (!chnl)
	return 0;//ERR_CHECK

chnl->conn = ci;
chnl->outgoing = is_outgoing;
memcpy(chnl->peer_id, peer_id, PICA_ID_SIZE);

chnl->acc = ci->acc;

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

static int establish_data_connection(struct PICA_c2c *chnl)
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
/////////////////state_
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
        memcpy(mp->tail, chnl->acc->id, PICA_ID_SIZE);//вызвывающий
		memcpy(mp->tail + PICA_ID_SIZE, chnl->peer_id, PICA_ID_SIZE);//вызываемый
		}
	else
		{
		memcpy(mp->tail, chnl->peer_id, PICA_ID_SIZE);//вызвывающий
        memcpy(mp->tail + PICA_ID_SIZE, chnl->acc->id, PICA_ID_SIZE);//вызываемый
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

chnl->ssl=SSL_new(chnl->acc->ctx);

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
/////////////////state_
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

{
unsigned long arg = 1;

IOCTLSETNONBLOCKINGSOCKET(chnl->sck_data, &arg);
}

return PICA_OK;

error_ret_:
error_ret:
callbacks.channel_failed(chnl->peer_id);
PICA_close_channel(chnl);
return err_ret;
}

static unsigned int procmsg_INITRESP_c2c(unsigned char* buf, unsigned int nb,void* p)
{
}

static unsigned int procmsg_INITRESP(unsigned char* buf, unsigned int nb,void* p)
{
struct PICA_c2n *ci=(struct PICA_c2n *)p;

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
struct PICA_c2n *ci=(struct PICA_c2n *)p;
struct PICA_proto_msg *mp;
unsigned char *peer_id = buf + 2;


	if (callbacks.accept_cb(peer_id))
		{
		struct PICA_c2c *_chnl;
		
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
struct PICA_c2n *ci=(struct PICA_c2n *)p;
struct PICA_c2c *ipt,*rq=0;
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
struct PICA_c2n *ci=(struct PICA_c2n *)p;
struct PICA_c2c *ipt,*rq=0;
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
struct PICA_c2c *chan = (struct PICA_c2c *)p;
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
struct PICA_c2c *chan = (struct PICA_c2c *)p;

callbacks.msgok_cb(chan -> peer_id);
return 1;
}

unsigned int procmsg_PINGREQ(unsigned char* buf,unsigned int nb,void* p)
{
struct PICA_c2n *ci=(struct PICA_c2n *)p;
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

int PICA_deny_file(struct PICA_c2c *chan)
{
struct PICA_proto_msg *mp;

if (chan->recvfilestate != PICA_CHANRECVFILESTATE_WAITACCEPT)
    return PICA_ERRINVARG;

chan->recvfilestate = PICA_CHANRECVFILESTATE_IDLE;

mp = c2c_writebuf_push(chan, PICA_PROTO_DENIEDFILE, PICA_PROTO_DENIEDFILE_SIZE);

if (mp)
    {
    RAND_bytes(mp->tail, 2);
    }
else
    return PICA_ERRNOMEM;

return PICA_OK;
}

int PICA_accept_file(struct PICA_c2c *chan, char *filename, unsigned int filenamesize)
{
struct PICA_proto_msg *mp;
int ret;

if (chan->recvfilestate != PICA_CHANRECVFILESTATE_WAITACCEPT)
    return PICA_ERRINVARG;

//open file
ret = PICA_recvfile_open_write(chan, filename, filenamesize);

if (ret != PICA_OK)
    return ret;

mp = c2c_writebuf_push(chan, PICA_PROTO_ACCEPTEDFILE, PICA_PROTO_ACCEPTEDFILE_SIZE);

if (mp)
    {
    RAND_bytes(mp->tail, 2);
    }
else
    return PICA_ERRNOMEM;

chan->recvfilestate  = PICA_CHANRECVFILESTATE_RECEIVING;

return PICA_OK;
}

static unsigned int procmsg_SENDFILEREQUEST(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_c2c *chan = (struct PICA_c2c *)p;
struct PICA_proto_msg *mp;
int ret;

char *filename = buf + 12;
unsigned int filenamesize = *(uint16_t*)(buf + 2) - 8;

if (chan->recvfilestate != PICA_CHANRECVFILESTATE_IDLE)
	return 0;

chan->recvfilestate  = PICA_CHANRECVFILESTATE_WAITACCEPT;
chan->recvfile_size = *(uint64_t*)(buf + 4);
chan->recvfile_pos = 0;

ret = callbacks.accept_file_cb(chan->peer_id, *(uint64_t*)(buf + 4), filename, filenamesize);

if (ret > 0)
	{
    if (ret == 1)
        {
        if (PICA_accept_file(chan, filename, filenamesize) != PICA_OK)
            if (PICA_deny_file(chan) != PICA_OK)
                return 0;
        }

	}
else
    {
    if (PICA_deny_file(chan) != PICA_OK)
        return 0;
    }

return 1;
}

static unsigned int procmsg_ACCEPTEDFILE(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_c2c *chan = (struct PICA_c2c *)p;

if (chan->sendfilestate != PICA_CHANSENDFILESTATE_SENTREQ)
    return 0;

callbacks.accepted_file_cb(chan->peer_id);

chan->sendfilestate = PICA_CHANSENDFILESTATE_SENDING;

return 1;
}

static unsigned int procmsg_DENIEDFILE(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_c2c *chan = (struct PICA_c2c *)p;

if (chan->sendfilestate != PICA_CHANSENDFILESTATE_SENTREQ)
    return 0;

callbacks.denied_file_cb(chan->peer_id);

chan->sendfilestate = PICA_CHANSENDFILESTATE_IDLE;

fclose(chan->sendfile_stream);
chan->sendfile_stream = NULL;

return 1;
}

static unsigned int procmsg_FILEFRAGMENT(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_c2c *chan = (struct PICA_c2c *)p;

/*
 *Allow incoming FILEFRAGMENTs while paused
 */
if (chan->recvfilestate != PICA_CHANRECVFILESTATE_RECEIVING
    && chan->recvfilestate != PICA_CHANRECVFILESTATE_PAUSED)
    return 0;

if (nb - 4 !=  *(uint16_t*)(buf + 2))
    return 0;

if (fwrite(buf + 4, nb - 4, 1, chan->recvfile_stream) != 1)
    {
    struct PICA_proto_msg *mp;

    fclose(chan->recvfile_stream);
    chan->recvfile_stream = NULL;

    chan->recvfilestate = PICA_CHANRECVFILESTATE_IDLE;

    if (PICA_OK != PICA_send_filecontrol(chan, PICA_PROTO_FILECONTROL_VOID, PICA_PROTO_FILECONTROL_IOERROR))
        return 0;
    }

chan->recvfile_pos += (nb - 4);


if (chan->recvfile_pos > chan->recvfile_size) //received more than file_size
    return 0;

callbacks.file_progress(chan->peer_id, 0, chan->recvfile_pos);

if (chan->recvfile_pos == chan->recvfile_size)
    {
    chan->recvfilestate = PICA_CHANRECVFILESTATE_IDLE;
    fclose(chan->recvfile_stream);
    chan->recvfile_stream = NULL;

    callbacks.file_finished(chan->peer_id, 0);
    }

return 1;
}

static unsigned int procmsg_FILECONTROL(unsigned char* buf, unsigned int nb, void* p)
{
struct PICA_c2c *chan = (struct PICA_c2c *)p;
unsigned int sender_cmd, receiver_cmd;

sender_cmd = buf[2];
receiver_cmd = buf[3];

fprintf(stderr, "procmsg_FILECONTROL sender_cmd = %i receiver_cmd = %i\n", sender_cmd, receiver_cmd);//debug

if ((chan->sendfilestate == PICA_CHANSENDFILESTATE_IDLE || chan->sendfilestate == PICA_CHANSENDFILESTATE_SENTREQ)
     && receiver_cmd != PICA_PROTO_FILECONTROL_VOID)
    return 0;

if (chan->recvfilestate == PICA_CHANRECVFILESTATE_IDLE && sender_cmd != PICA_PROTO_FILECONTROL_VOID)
    return 0;

fprintf(stderr, "procmsg_FILECONTROL  switch(sender_cmd)\n");

switch(sender_cmd)
    {
    case PICA_PROTO_FILECONTROL_PAUSE:
    if (chan->recvfilestate == PICA_CHANRECVFILESTATE_RECEIVING)
        chan->recvfilestate = PICA_CHANRECVFILESTATE_PAUSED;
    else
        return 0;
    break;

    case PICA_PROTO_FILECONTROL_RESUME:
    if (chan->recvfilestate == PICA_CHANRECVFILESTATE_PAUSED)
        chan->recvfilestate = PICA_CHANRECVFILESTATE_RECEIVING;
    else
        return 0;
    break;

    case PICA_PROTO_FILECONTROL_CANCEL:
    case PICA_PROTO_FILECONTROL_IOERROR:
    chan->recvfilestate = PICA_CHANRECVFILESTATE_IDLE;
    fclose(chan->recvfile_stream);
    chan->recvfile_stream = NULL;
    break;

    case PICA_PROTO_FILECONTROL_VOID:
    break;

    default:
    return 0;
    }

fprintf(stderr, "procmsg_FILECONTROL  switch(receiver_cmd)\n");
switch(receiver_cmd)
    {
    case PICA_PROTO_FILECONTROL_PAUSE:
    if (chan->sendfilestate == PICA_CHANSENDFILESTATE_SENDING)
        chan->sendfilestate = PICA_CHANSENDFILESTATE_PAUSED;
    else
        return 0;
    break;

    case PICA_PROTO_FILECONTROL_RESUME:
    if (chan->sendfilestate == PICA_CHANSENDFILESTATE_PAUSED)
        chan->sendfilestate = PICA_CHANSENDFILESTATE_SENDING;
    else
        return 0;
    break;

    case PICA_PROTO_FILECONTROL_CANCEL:
    case PICA_PROTO_FILECONTROL_IOERROR:
    chan->sendfilestate = PICA_CHANSENDFILESTATE_IDLE;
    fclose(chan->sendfile_stream);
    chan->sendfile_stream = NULL;
    break;

    case PICA_PROTO_FILECONTROL_VOID:
    break;

    default:
    return 0;
    }

fprintf(stderr, "procmsg_FILECONTROL  calling callback\n");
callbacks.file_control(chan->peer_id, sender_cmd, receiver_cmd);

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

static int check_pkey_passphrase(const char *pkey_file,
                                 int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
                                 void *userdata)
{
RSA *rsa;
FILE *f;


f = fopen(pkey_file, "r");

if (!f)
    return PICA_ERRINVPKEYFILE;

rsa = PEM_read_RSAPrivateKey(f, NULL, password_cb, userdata);

fclose(f);

if (!rsa)
    {
    unsigned long int e = ERR_get_error();

    // 0x6065064 is an OpenSSL error code corresponding to decryption failure
    if (e == 0x6065064 || strcmp("bad decrypt", ERR_reason_error_string(e)) == 0)
        return PICA_ERRINVPKEYPASSPHRASE;

        return PICA_ERRINVPKEYFILE;
    }

RSA_free(rsa);

return PICA_OK;
}

// Opens socket to listen for incoming direct c2c connections bypassing nodes
//
// acc - pointer to opened account
//
// public_addr - DNS name or IP address string,
// it is an address that will be used for connecting from global Internet.
// Should be set to router's external IP if computer running pica-client is located behind the NAT.
//
// public_port - TCP port for incoming connections from global Internet
//
// local_port - TCP port that will be actually opened on computer listening for direct c2c connections
//
// l - address of pointer that will be filled with address of created PICA_listener structure
//
int PICA_new_listener(const struct PICA_acc *acc,const char *public_addr, int public_port, int local_port, struct PICA_listener **l)
{
    int ret, ret_err, flag;
    struct PICA_listener *lst = NULL;
    struct sockaddr_in s;


    if (!acc || !public_addr || public_port <= 0 || public_port > 65535 || local_port <= 0 || local_port > 65535 || !l)
        return PICA_ERRINVARG;

    lst = *l = (struct PICA_listener*)calloc(sizeof(struct PICA_listener), 1);

    if (!lst)
        return PICA_ERRNOMEM;

    lst->public_addr_dns = strdup(public_addr);

    if (!lst->public_addr_dns)
        return PICA_ERRNOMEM;

    lst->public_addr_ipv4 = inet_addr(public_addr);

    lst->acc = acc;
    lst->public_port = public_port;
    lst->local_port = local_port;

    lst->sck_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //(1)

    if (lst->sck_listener == SOCKET_ERROR)
        {
        ret_err = PICA_ERRSOCK;
        goto error_ret_1;
        }

    flag = 1;
    setsockopt(lst->sck_listener, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    memset(&s, 0, sizeof(s));
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = INADDR_ANY;
    s.sin_port = htons(local_port);

    ret = bind(lst->sck_listener, (const struct sockaddr*) &s, sizeof(s));//(2)

    if (ret == SOCKET_ERROR)
        {
        ret_err = PICA_ERRSOCK;
        goto error_ret_2;
        }

    ret = listen(lst->sck_listener, 10);

     if (ret == SOCKET_ERROR)
        {
        ret_err = PICA_ERRSOCK;
        goto error_ret_2;
        }

    return PICA_OK;

    error_ret_2: //(2)

    CLOSE(lst->sck_listener);

    error_ret_1: //(1)

    free(lst->public_addr_dns);
    free(lst);
    *l = 0;

    return ret_err;
}

int PICA_open_acc(const char *cert_file,
                  const char *pkey_file,
                  const char *dh_param_file,
                  int (*password_cb)(char *buf, int size, int rwflag, void *userdata),
                  struct PICA_acc **acc)
{
int ret,ret_err;
DH *dh = NULL;
FILE *dh_file = NULL;
struct PICA_acc *a = NULL;


if (!(cert_file && pkey_file && dh_param_file && acc))
    return PICA_ERRINVARG;

a = *acc = (struct PICA_acc*)calloc(sizeof(struct PICA_acc), 1);//(1)

if (!a)
    return PICA_ERRNOMEM;

a->ctx = SSL_CTX_new(TLSv1_2_method());//(2)

if (!a->ctx)
    {
    ret_err=PICA_ERRSSL;
    goto error_ret_1;
    }

dh_file = fopen(dh_param_file, "r");

if (dh_file)
    {
    dh = PEM_read_DHparams(dh_file, NULL, NULL, NULL);
    fclose(dh_file);
    }

if (1 != SSL_CTX_set_tmp_dh(a->ctx, dh))
{
    ret_err = PICA_ERRSSL;
    goto error_ret_2;
}

DH_free(dh);

if (!PICA_get_id_from_cert_file(cert_file, a->id))
    {
    ret_err = PICA_ERRINVCERT;
    goto error_ret_2;
    }

ret = check_pkey_passphrase(pkey_file, password_cb, a->id);

if (ret != PICA_OK)
    {
    ret_err = ret;
    goto error_ret_2;
    }

////<<
if (password_cb)
    {
    SSL_CTX_set_default_passwd_cb(a->ctx, password_cb);
    SSL_CTX_set_default_passwd_cb_userdata(a->ctx, a->id);
    }

ret = SSL_CTX_use_certificate_file(a->ctx, cert_file, SSL_FILETYPE_PEM);

if (ret != 1)
    {
    ret_err=PICA_ERRSSL;
    goto error_ret_2;
    }

ret = SSL_CTX_use_PrivateKey_file(a->ctx, pkey_file, SSL_FILETYPE_PEM);
if (ret != 1)
       {
       ret_err = PICA_ERRSSL;
       goto error_ret_2;
       }
////<<

//ret=SSL_CTX_load_verify_locations(cid->ctx,CA_file,0/*"trustedCA/"*/);
//printf("loadverifylocations ret=%i\n",ret);//debug
//SSL_CTX_set_client_CA_list(cid->ctx,SSL_load_client_CA_file(CA_file));

ret = SSL_CTX_set_cipher_list(a->ctx,"DHE-RSA-AES256-GCM-SHA384:DHE-RSA-CAMELLIA256-SHA");

if (ret != 1)
{
    ret_err = PICA_ERRSSL;
    goto error_ret_2;
}

return PICA_OK;

error_ret_2: //(2)
SSL_CTX_free(a->ctx);

error_ret_1: //(1)
free(a);
*acc = 0;

return ret_err;
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
//ci -указатель на указатель на структуру PICA_c2n, который будет проинициализирован при успешном выполнении функции
//Память, выделенная под структуру, освобождается при вызове PICA_close_connection
//Возвращаемое значение - код завершения
//PICA_OK - успешное завершение функции
//....

int PICA_new_connection(const struct PICA_acc *acc, const char *nodeaddr,
                        unsigned int port,
                        struct PICA_c2n **ci)
{
int ret,ret_err;
struct PICA_c2n *cid;
struct sockaddr_in a;


if (!(nodeaddr && acc && port && ci))
	return PICA_ERRINVARG;


cid=*ci=(struct PICA_c2n*)calloc(sizeof(struct PICA_c2n),1);//(1)
if (!cid)
	return PICA_ERRNOMEM;

cid->acc = acc;

cid->read_buf = calloc(PICA_CONNREADBUFSIZE, 1);//(2)
if (!cid->read_buf)
    {
    ret_err = PICA_ERRNOMEM;
    goto error_ret_1;
    }

cid->ssl_comm=SSL_new(acc->ctx);//(3)

if (!cid->ssl_comm)
	{
	ret_err=PICA_ERRSSL;
    goto error_ret_2;
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
    goto error_ret_3;
	}

cid->state = PICA_CONNSTATE_CONNECTING;

ret=connect(cid->sck_comm,(struct sockaddr*)&a,sizeof(struct sockaddr_in));
/////////////////state_

if (ret==SOCKET_ERROR)
	{
	ret_err=PICA_ERRSOCK;
    goto error_ret_4;
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
    goto error_ret_4;
	}
}

//cid->state = PICA_CONNSTATE_WAITINGREP;

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
    goto error_ret_4;
	}

//printf("6\n");//debug

ret=SSL_set_fd(cid->ssl_comm,cid->sck_comm);

//printf("SSL_set_fd=%i\n",ret);

ret=SSL_accept(cid->ssl_comm);
/////////////////state_
cid->state = PICA_CONNSTATE_WAITINGTLS;
if (ret!=1)
	{
	//printf("SSL_accept  ret=%i\n  SSL_get_error=%i\n",ret,SSL_get_error(cid->ssl_comm,ret));//debug
		//ERR_CHECK
	ret_err=PICA_ERRSSL;
    goto error_ret_4;
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

{
unsigned long arg = 1;

IOCTLSETNONBLOCKINGSOCKET(cid->sck_comm, &arg);
}

return PICA_OK;


error_ret_4: //(4)
    SHUTDOWN(cid->sck_comm);
    CLOSE(cid->sck_comm);

error_ret_3: //(3)
    SSL_free(cid->ssl_comm);

error_ret_2: //(2)
    free(cid->read_buf);

error_ret_1: //(1)
    free(cid);
    *ci=0;

return ret_err;
}



//создает ИСХОДЯЩИЙ логический зашифрованный канал связи с указанным собеседником, если тот доступен
// в данный момент.
int PICA_create_channel(struct PICA_c2n *ci,const unsigned char *peer_id, struct PICA_listener *l,struct PICA_c2c **chn)
{
struct PICA_proto_msg *mp;
struct PICA_c2c *chnl;

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

static void fdset_add(fd_set *fds, int fd, int *max)
{
FD_SET(fd, fds);

if (fd > *max)
    *max = fd;
}

int PICA_event_loop(struct PICA_c2n **connections, struct PICA_listener **listeners, int timeout)
{
fd_set rfds, wfds;
struct timeval tv;
int ret, nfds = 0;
struct PICA_c2n **ic2n;
struct PICA_listener **ilst;

tv.tv_sec = timeout / 1000;
tv.tv_usec = (timeout % 1000) * 1000;

FD_ZERO(&rfds);
FD_ZERO(&wfds);

ilst = listeners;

while(ilst && *ilst)
    {
    fdset_add(&rfds, (*ilst)->sck_listener, &nfds);
    ilst++;
    }

ic2n = connections;

while(ic2n && *ic2n)
    {
    struct PICA_c2c *ic2c;

    fdset_add(&rfds, (*ic2n)->sck_comm, &nfds);

    if ((*ic2n)->write_pos)
        fdset_add(&wfds, (*ic2n)->sck_comm, &nfds);

    ic2c = (*ic2n)->chan_list_head;

    while(ic2c)
        {
        if (ic2c->state == PICA_CHANSTATE_ACTIVE)
            {
            fdset_add(&rfds, ic2c->sck_data, &nfds);

            if (ic2c->write_pos)
                fdset_add(&wfds, ic2c->sck_data, &nfds);
            }

        ic2c = ic2c->next;
        }

    ic2n++;
    }



ret = select(nfds + 1, &rfds, &wfds, NULL, &tv);


}

int PICA_read(struct PICA_c2n *ci,int timeout)
{
fd_set fds;
struct timeval tv={0,1000};
struct PICA_c2c *ipt, *kill_ptr = 0;
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
                puts("PICA_read_c2c() failed");//debug
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

int PICA_write(struct PICA_c2n *ci)
{
struct PICA_c2c *ipt, *kill_ptr = 0;
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
            fprintf(stderr,"PICA_write_c2c() failed, ret = %i\n", ret);//debug
			callbacks.channel_closed_cb(ipt->peer_id, ret);
			kill_ptr = ipt;
			}
		}
    else if (ipt->sendfilestate == PICA_CHANSENDFILESTATE_SENDING)
        {
        if (PICA_OK != (ret = PICA_send_file_fragment(ipt)))
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

int PICA_write_ssl(SSL *ssl, unsigned char *buf, unsigned int *ppos, unsigned int *btw)
{
int ret;

if (*ppos > 0 )
	{
    if (*btw == 0)
        *btw = *ppos;

    ret = SSL_write( ssl, buf, *btw);
	
	if (ret > 0)
		{
		if (ret < *ppos)
			memmove( buf, buf + ret, *ppos - ret);
		*ppos -= ret;
        *btw = 0;
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

int PICA_write_c2n(struct PICA_c2n *ci)
{
int ret = PICA_ERRINVARG;

if (PICA_CONNSTATE_CONNECTING == ci->state)
	ret = PICA_write_socket( ci->sck_comm, ci->write_buf, &ci->write_pos);
else if (PICA_CONNSTATE_CONNECTED == ci->state)
    ret = PICA_write_ssl( ci->ssl_comm, ci->write_buf, &ci->write_pos, &ci->write_sslbytestowrite);

return ret;
}

int PICA_write_c2c(struct PICA_c2c *chn)
{
int ret = PICA_ERRINVARG;

if (PICA_CHANSTATE_ACTIVE != chn->state)
	ret = PICA_write_socket( chn->sck_data, chn->write_buf, &chn->write_pos);
else
    ret = PICA_write_ssl( chn->ssl, chn->write_buf, &chn->write_pos, &chn->write_sslbytestowrite);

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

int PICA_read_c2c(struct PICA_c2c *chn)
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
int PICA_read_c2n(struct PICA_c2n *ci) 
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

struct PICA_proto_msg* c2n_writebuf_push(struct PICA_c2n *ci, unsigned int msgid, unsigned int size)
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

struct PICA_proto_msg* c2c_writebuf_push(struct PICA_c2c *chn, unsigned int msgid, unsigned int size)
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

int PICA_send_msg(struct PICA_c2c *chn, char *buf,unsigned int len)
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

int PICA_send_file(struct PICA_c2c *chn, const char *filepath)
{
struct PICA_proto_msg *mp;
size_t namelen;
uint64_t file_size;
int ret;
const char *filename;//file name part of the filepath

if (chn->sendfilestate != PICA_CHANSENDFILESTATE_IDLE)
	return PICA_ERRFILETRANSFERINPROGRESS;

if (filepath == NULL)
	return PICA_ERRINVFILENAME;

#ifndef WIN32
filename = strrchr(filepath, '/');
#else
filename = strrchr(filepath, '\\');
#endif

if (filename)
    filename++;
else
    filename = filepath;


namelen = strlen(filename);

if (namelen  == 0 || namelen > PICA_PROTO_C2CMSG_MAXFILENAMESIZE)
	return PICA_ERRINVFILENAME;

if ((ret = PICA_sendfile_open_read(chn, filepath, &file_size)) != PICA_OK)
    return ret;

if ((mp = c2c_writebuf_push(chn, PICA_PROTO_SENDFILEREQUEST, namelen + 12)))
	{
    *((uint16_t*)mp->tail) = namelen + 8;
    *((uint64_t*)(mp->tail + 2)) = file_size;

    memcpy(mp->tail + 10, filename, namelen);
	}
else
	return PICA_ERRNOMEM;

chn->sendfilestate = PICA_CHANSENDFILESTATE_SENTREQ;
chn->sendfile_size = file_size;
chn->sendfile_pos = 0;

return PICA_OK;
}

int PICA_pause_file(struct PICA_c2c *chan, int sending)
{
int senderctl = PICA_PROTO_FILECONTROL_VOID;
int receiverctl = PICA_PROTO_FILECONTROL_VOID;

fprintf(stderr, "PICA_pause_file(%p, %i)\n", chan, sending);//debug

if (sending)
    {
    if (chan->sendfilestate != PICA_CHANSENDFILESTATE_SENDING)
        return PICA_ERRINVARG;

    chan->sendfilestate = PICA_CHANSENDFILESTATE_PAUSED;

    senderctl = PICA_PROTO_FILECONTROL_PAUSE;
    }
    else
    {
    if (chan->recvfilestate != PICA_CHANRECVFILESTATE_RECEIVING)
        return PICA_ERRINVARG;

    chan->recvfilestate = PICA_CHANRECVFILESTATE_PAUSED;

    receiverctl = PICA_PROTO_FILECONTROL_PAUSE;
    }

return PICA_send_filecontrol(chan, senderctl, receiverctl);
}

int PICA_resume_file(struct PICA_c2c *chan, int sending)
{
int senderctl = PICA_PROTO_FILECONTROL_VOID;
int receiverctl = PICA_PROTO_FILECONTROL_VOID;

if (sending)
    {
    if (chan->sendfilestate != PICA_CHANSENDFILESTATE_PAUSED)
        return PICA_ERRINVARG;

    chan->sendfilestate = PICA_CHANSENDFILESTATE_SENDING;

    senderctl = PICA_PROTO_FILECONTROL_RESUME;
    }
    else
    {
    if (chan->recvfilestate != PICA_CHANRECVFILESTATE_PAUSED)
        return PICA_ERRINVARG;

    chan->recvfilestate = PICA_CHANRECVFILESTATE_RECEIVING;

    receiverctl = PICA_PROTO_FILECONTROL_RESUME;
    }

return PICA_send_filecontrol(chan, senderctl, receiverctl);
}

int PICA_cancel_file(struct PICA_c2c *chan, int sending)
{
int senderctl = PICA_PROTO_FILECONTROL_VOID;
int receiverctl = PICA_PROTO_FILECONTROL_VOID;

if (sending)
    {
    if (chan->sendfilestate != PICA_CHANSENDFILESTATE_SENDING
        && chan->sendfilestate != PICA_CHANSENDFILESTATE_PAUSED)
        return PICA_ERRINVARG;

    fclose(chan->sendfile_stream);
    chan->sendfile_stream = NULL;
    chan->sendfilestate = PICA_CHANSENDFILESTATE_IDLE;

    senderctl = PICA_PROTO_FILECONTROL_CANCEL;
    }
    else
    {
    if (chan->recvfilestate != PICA_CHANRECVFILESTATE_RECEIVING
        && chan->recvfilestate != PICA_CHANRECVFILESTATE_PAUSED)
        return PICA_ERRINVARG;

    fclose(chan->recvfile_stream);
    chan->recvfile_stream = NULL;
    chan->recvfilestate = PICA_CHANRECVFILESTATE_IDLE;

    receiverctl = PICA_PROTO_FILECONTROL_CANCEL;
    }

return PICA_send_filecontrol(chan, senderctl, receiverctl);
}

int PICA_send_filecontrol(struct PICA_c2c *chan, int senderctl, int receiverctl)
{
struct PICA_proto_msg *mp;

fprintf(stderr, "PICA_send_filecontrol(%p, %i, %i)\n", chan, senderctl, receiverctl);//debug

if ((mp = c2c_writebuf_push(chan, PICA_PROTO_FILECONTROL, PICA_PROTO_FILECONTROL_SIZE)))
    {
    mp->tail[0] = senderctl; //sender's command
    mp->tail[1] = receiverctl; //receiver's command
    }
else
    return PICA_ERRNOMEM;

return PICA_OK;
}

int PICA_send_file_fragment(struct PICA_c2c *chn)
{
struct PICA_proto_msg *mp;
char buf[PICA_FILEFRAGMENTSIZE];
size_t fragment_size;

if (chn->sendfilestate != PICA_CHANSENDFILESTATE_SENDING)
    return PICA_ERRFILETRANSFERNOTINPROGRESS;

fragment_size = fread(buf, 1, PICA_FILEFRAGMENTSIZE, chn->sendfile_stream);

if (fragment_size == 0)
    {
    fclose(chn->sendfile_stream);
    chn->sendfile_stream = NULL;
    chn->sendfilestate = PICA_CHANSENDFILESTATE_IDLE;

    PICA_send_filecontrol(chn, PICA_PROTO_FILECONTROL_IOERROR, PICA_PROTO_FILECONTROL_VOID);

    return PICA_ERRFILEIO;
    }

if ((mp = c2c_writebuf_push(chn, PICA_PROTO_FILEFRAGMENT, fragment_size + 4)))
    {
    *((uint16_t*)mp->tail) = fragment_size;

    memcpy(mp->tail + 2, buf, fragment_size);
    }
else
    {
    return PICA_ERRNOMEM;
    }

chn->sendfile_pos += fragment_size;

callbacks.file_progress(chn->peer_id, chn->sendfile_pos, 0);

if (chn->sendfile_pos >= chn->sendfile_size)
    {
    chn->sendfilestate = PICA_CHANSENDFILESTATE_IDLE;
    fclose(chn->sendfile_stream);
    chn->sendfile_stream = NULL;

    callbacks.file_finished(chn->peer_id, 1);
    }

if (chn->sendfile_pos > chn->sendfile_size)
    return PICA_ERRFILEFRAGMENTCROSSEDSIZE;

return PICA_OK;
}

void PICA_close_channel(struct PICA_c2c *chn)
{
struct PICA_c2c *ipt;

fprintf(stderr, "PICA_close_channel(%p)\n", chn);//debug
	
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

if (chn -> sendfile_stream)
    fclose(chn -> sendfile_stream);

if (chn -> recvfile_stream)
    fclose(chn -> recvfile_stream);

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

void PICA_close_connection(struct PICA_c2n *cid)
{
struct PICA_c2c *ipt;

while((ipt = cid->chan_list_head))
	PICA_close_channel(ipt);

SSL_shutdown(cid->ssl_comm);
//printf("4 SSL_free(%X)\n",cid->ssl_comm);//debug
SSL_free(cid->ssl_comm);
SHUTDOWN(cid->sck_comm);
CLOSE(cid->sck_comm);

free(cid->read_buf);

if (cid->write_buf)
	free(cid->write_buf);

free(cid);
}

void PICA_close_acc(struct PICA_acc *a)
{
SSL_CTX_free(a->ctx);
free(a);
}

void PICA_close_listener(struct PICA_listener *l)
{
CLOSE(l->sck_listener);
free(l->public_addr_dns);
free(l);
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


int PICA_sendfile_open_read(struct PICA_c2c *chn, const char *filename_utf8, uint64_t *file_size)
{
#ifdef WIN32
{
wchar_t *wfilename = NULL;
int wlen;

wlen = MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1, NULL, 0);

if (wlen == 0 || wlen == 0xFFFD)
    return PICA_ERRINVFILENAME;

wfilename = (wchar_t*)malloc(wlen * sizeof(wchar_t));

if (!wfilename)
    return PICA_ERRNOMEM;

MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1, wfilename, wlen);

chn->sendfile_stream = _wfopen(wfilename, L"rb");

free(wfilename);

if (chn->sendfile_stream == NULL)
    return PICA_ERRFILEOPEN;

if (_fseeki64(chn->sendfile_stream, 0, SEEK_END) != 0)
    return PICA_ERRFILEOPEN;

*file_size = _ftelli64(chn->sendfile_stream);

if (_fseeki64(chn->sendfile_stream, 0, SEEK_SET) != 0)
    return PICA_ERRFILEOPEN;
}
#else
chn->sendfile_stream = fopen(filename_utf8, "rb");

if (chn->sendfile_stream == NULL)
    return PICA_ERRFILEOPEN;

if (fseeko(chn->sendfile_stream, 0, SEEK_END) != 0)
    return PICA_ERRFILEOPEN;

*file_size = ftello(chn->sendfile_stream);

if (fseeko(chn->sendfile_stream, 0, SEEK_SET) != 0)
    return PICA_ERRFILEOPEN;
#endif

return PICA_OK;
}

int PICA_recvfile_open_write(struct PICA_c2c *chn, const char *filename_utf8, unsigned int filenamesize)
{
/////////////
#ifdef WIN32
{
wchar_t *rfilename = NULL;
int rlen;

rlen = MultiByteToWideChar(CP_UTF8, 0, filename_utf8, filenamesize, NULL, 0);

if (rlen == 0 || rlen == 0xFFFD)
    return PICA_ERRINVFILENAME;

rlen++;//space for terminating zero character

rfilename = malloc(rlen * sizeof(wchar_t));

if (!rfilename)
    return PICA_ERRNOMEM;

MultiByteToWideChar(CP_UTF8, 0, filename_utf8, filenamesize, rfilename, rlen);

rfilename[rlen - 1] = L'\0';

chn->recvfile_stream = _wfopen(rfilename, L"wb");

free(rfilename);

if (chn->recvfile_stream == NULL)
    return PICA_ERRFILEOPEN;

}
#else
chn->recvfile_stream = fopen(filename_utf8, "wb");

if (chn->recvfile_stream == NULL)
    return PICA_ERRFILEOPEN;
#endif
/////////////
return PICA_OK;
}
