#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../PICA_client.h"


struct sockaddr_in a;
struct PICA_acc *acc;
struct PICA_c2n *c;
struct PICA_c2c *chn;

int _outgoing_chnl=0;

unsigned char buf[64*1024];

int accept_cb(const unsigned char *id)
{
printf("accept_cb: %s\n",PICA_id_to_base64(id, NULL));
return 1;
}

//получение сообщения.
void newmsg_cb(const unsigned char *peer_id, const char* msgbuf,unsigned int nb,int type)
{
memcpy(buf,msgbuf,nb);
buf[nb]=0;
printf(" %s: ",PICA_id_to_base64(peer_id, NULL));
puts(buf);
}
//получение подтверждения о доставке сообщения
void msgok_cb(const unsigned char *peer_id)
{
puts("[V]");
}
//создание канала с собеседником
void c2c_established_cb(const unsigned char *peer_id)
{
puts("c2c_established_cb");
}

void c2c_failed_cb(const unsigned char *peer_id)
{
printf("Failed to create c2c to %s\n",PICA_id_to_base64(peer_id, NULL));
chn=0;
}

//входящий запрос на создание канала от пользователя с номером caller_id
//<<int accept_cb(unsigned int caller_id);
//запрошенный пользователь не найден, в оффлайне или отказался от общения
void notfound_cb(const unsigned char *callee_id)
{
puts("notfound_cb");
}

void c2c_closed_cb(const unsigned char *peer_id, int reason)
{
printf("c2c_closed_cb( peer_id = %s, reason = %i)\n", PICA_id_to_base64(peer_id, NULL), reason);
}

void nodelist_cb(int type, void *addr_bin, const char *addr_str, unsigned int port)
{
switch(type)
		{
	    	case 0xA0:
		printf("node (IP %s port %u)\n", addr_str, port);
		break;
		
	    	case 0xA1:
		printf("node (IPv6 %s port %u)", addr_str, port);
		break;
		
	    	case 0xA2:
		printf("node (hostname %s port %u)\n", addr_str, port);
		break;
		
	    	default:
		puts("unknown node address type");
		return;
		}
}

int peer_cert_verify_cb(const unsigned char *peer_id, const char *cert_pem, unsigned int nb)
{
    printf("peer's certificate (%s): \n%.*s\n", PICA_id_to_base64(peer_id, NULL), nb,cert_pem);
    return 1;
}

int accept_file_cb(const unsigned char  *peer_id, uint64_t  file_size, const char *filename, unsigned int filename_size)
{
    printf("accept_file_cb: peer_id %s\n", PICA_id_to_base64(peer_id, NULL));
    return 0;
}

void accepted_file_cb(const unsigned char *peer_id)
{
    printf("accepted_file_cb: peer_id %s \n", PICA_id_to_base64(peer_id, NULL));
}

void denied_file_cb(const unsigned char *peer_id)
{
    printf("denied_file_cb: peer_id %s \n", PICA_id_to_base64(peer_id, NULL));
}

void file_progress(const unsigned char *peer_id, uint64_t sent, uint64_t received)
{
    printf("file_progress: peer_id %s\n", PICA_id_to_base64(peer_id, NULL));
}

void file_control(const unsigned char *peer_id, unsigned int sender_cmd, unsigned int receiver_cmd)
{
    printf("file_control: peer_id %s sender_cmd %u receiver_cmd %u \n", PICA_id_to_base64(peer_id, NULL), sender_cmd, receiver_cmd);
}

void file_finished(const unsigned char *peer_id, int sending)
{
    printf("file_finished: peer_id %s\n", PICA_id_to_base64(peer_id, NULL));
}

void c2n_established_cb(struct PICA_c2n *c2n)
{
    printf("c2n_established_cb: %p\n peer_id %s\n", c2n, PICA_id_to_base64(c2n->acc->id, NULL));
}

void c2n_failed_cb(struct PICA_c2n *c2n)
{
    printf("c2n_failed_cb: %p\n", c2n);
}

void c2n_closed_cb(struct PICA_c2n *c2n)
{
    printf("c2n_closed_cb: %p\n", c2n);
}

void listener_error(struct PICA_listener *lst, int errorcode)
{
    printf("listener_error: %p error code %i\n", lst, errorcode);
}

struct PICA_client_callbacks cbs = {
    newmsg_cb, 
    msgok_cb, 
    c2c_established_cb,
    c2c_failed_cb,
    accept_cb, 
    notfound_cb, 
    c2c_closed_cb,
    nodelist_cb,
    peer_cert_verify_cb,
    accept_file_cb,
    accepted_file_cb,
    denied_file_cb,
    file_progress,
    file_control,
    file_finished,
    c2n_established_cb,
    c2n_failed_cb,
    c2n_closed_cb,
    listener_error
};

int main(int argc,char** argv)
{
int ret;
unsigned char peer_id[PICA_ID_SIZE];
struct PICA_c2n* c2ns[] = {NULL, NULL};

if (argc<4)
	{
	puts("usage: test_client      address port  cert_filename  [peer id]");
	return 0;
	}

puts(SSLeay_version(SSLEAY_VERSION));
printf("Calling PICA_init...\n");

PICA_client_init(&cbs);

// printf("setting address...\n");
// 
// memset(&a,0,sizeof(a));
// 
// a.sin_family      = AF_INET;
// a.sin_addr.s_addr = inet_addr (argv[1]);//("127.0.0.1");
// a.sin_port        = htons(51914);

printf("opening account...");
ret = PICA_open_acc(argv[3], argv[3],argv[3], NULL, &acc);

if (ret != PICA_OK)
{
    printf("failed to open account, ret = %i\n", ret);
    return 1;
}

printf("making connection...\n");

//PICA_new_c2n(const char *nodeaddr, unsigned int port, const char *CA_file, const char *cert_file, const char *pkey_file, const char* password, struct PICA_c2n **ci)
ret=PICA_new_c2n(acc, argv[1], atoi(argv[2]), /*"trusted_CA.pem"*/ &c);

ERR_print_errors_fp(stdout);

if (ret==PICA_OK)
printf("PICA_OK started c2n connection to node...\n");
else
{
printf("PICA_new_c2n: ret=%i\n",ret);
return 1;
}

c2ns[0] = c;

if (argc==5)
	{
	if (PICA_id_from_base64(argv[4], peer_id) == NULL)
	{
	    puts("invalid peer_id");
	    return 1;
	}

    printf("Creating c2c to %s...\n",PICA_id_to_base64(peer_id, NULL));
	
    ret=PICA_new_c2c(c,peer_id,NULL,&chn);
		//sleep(17);//timeout test
	if (ret!=PICA_OK)
		{
		printf("error: ret=%i\n",ret);
		return 1;
		}
	
	}

	while(PICA_event_loop(c2ns, NULL, 1500) == PICA_OK);
/*
do
	{
		if (chn && chn->state==PICA_CHANSTATE_ACTIVE)
		{
		unsigned int msglen;
		gets(buf);
		msglen=strlen(buf);
		
		ret=PICA_send_msg(chn,buf,msglen);

		if (ret!=PICA_OK)
			printf("PICA_send_msg ret=%i\n",ret);
		}
	}
while(PICA_read(c,100000) == PICA_OK && PICA_write(c) == PICA_OK);*/

/*
puts("Enter peer_id to talk with or . for waiting incoming sessions:");
gets(buf);
if (buf[0]=='.')
	{
		unsigned int br;
		ret=PICA_read_conn(c,accept_cb,&chn);

		if (ret==PICA_NEWCHAN)
			{
			puts("PICA_NEWCHAN!");
			}
		else
			printf("read_conn error: ret=%i\n",ret); 
		
		ret=PICA_read_msg(chn,buf,&br);
		switch(ret)
			{
			case PICA_MSGUTF8:
			printf("%u (%u bytes): ",c->id,br);
			puts(buf);
			break;
			case PICA_MSGOK:
			puts("[v]");
			break;
			default:
			printf("PICA_read_msg ret=%i\n",ret);
			}
	}
else
	{
	peer_id=atoi(buf);
    printf("Creating c2c to %u...\n",peer_id);
	
    ret=PICA_new_c2c(c,peer_id,&chn,1);

	if (ret==PICA_OK)
		{
		unsigned int msglen;
        puts("c2c established...");
		gets(buf);
		msglen=strlen(buf);
		
		ret=PICA_send_msg(chn,buf,msglen);
		if (ret!=PICA_OK)
			printf("PICA_send_msg ret=%i\n",ret);
		}
	else
	printf("error: ret=%i\n",ret);
	}

*/
sleep(10);

if (chn)
	{
	puts("--------");
    PICA_close_c2c(chn);
	}

PICA_close_c2n(c);

return 0;
}
