#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../PICA_client.h"


struct sockaddr_in a;
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
void newmsg_cb(const unsigned char *peer_id,char* msgbuf,unsigned int nb,int type)
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
void channel_established_cb(const unsigned char *peer_id)
{
puts("channel_established_cb");
}

void channel_failed_cb(const unsigned char *peer_id)
{
printf("Failed to create channel to %s\n",PICA_id_to_base64(peer_id, NULL));
chn=0;
}

//входящий запрос на создание канала от пользователя с номером caller_id
//<<int accept_cb(unsigned int caller_id);
//запрошенный пользователь не найден, в оффлайне или отказался от общения
void notfound_cb(const unsigned char *callee_id)
{
puts("notfound_cb");
}

void channel_closed_cb(const unsigned char *peer_id, int reason)
{
printf("channel_closed_cb( peer_id = %s, reason = %i)\n", PICA_id_to_base64(peer_id, NULL), reason);
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

struct PICA_client_callbacks cbs = {
    newmsg_cb, 
    msgok_cb, 
    channel_established_cb, 
    channel_failed_cb, 
    accept_cb, 
    notfound_cb, 
    channel_closed_cb,
    nodelist_cb,
    peer_cert_verify_cb
};

int main(int argc,char** argv)
{
int ret;
unsigned char peer_id[PICA_ID_SIZE];

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


printf("making connection...\n");

//PICA_new_connection(const char *nodeaddr, unsigned int port, const char *CA_file, const char *cert_file, const char *pkey_file, const char* password, struct PICA_c2n **ci)
ret=PICA_new_connection(argv[1], atoi(argv[2]), /*"trusted_CA.pem"*/ argv[3], argv[3],argv[3],NULL,&c);

ERR_print_errors_fp(stdout);

if (ret==PICA_OK)
printf("PICA_OK Connected to server...\n");
else
{
printf("PICA_new_connection: ret=%i\n",ret);
return 1;
}

if (argc==5)
	{
	if (PICA_id_from_base64(argv[4], peer_id) == NULL)
	{
	    puts("invalid peer_id");
	    return 1;
	}

	printf("Creating channel to %s...\n",PICA_id_to_base64(peer_id, NULL));
	
	ret=PICA_create_channel(c,peer_id,&chn);
		//sleep(17);//timeout test
	if (ret!=PICA_OK)
		{
		printf("error: ret=%i\n",ret);
		return 1;
		}
	
	}

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
while(PICA_read(c,100000) == PICA_OK && PICA_write(c) == PICA_OK);

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
	printf("Creating channel to %u...\n",peer_id);
	
	ret=PICA_create_channel(c,peer_id,&chn,1);

	if (ret==PICA_OK)
		{
		unsigned int msglen;
		puts("Channel established...");
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
	PICA_close_channel(chn);
	}

PICA_close_connection(c);

return 0;
}
