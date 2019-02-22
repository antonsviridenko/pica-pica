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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "../PICA_client.h"
#include "../PICA_proto.h"

struct sockaddr_in a;
struct PICA_acc *acc;
struct PICA_c2n *c;
struct PICA_c2c *chn;
enum PICA_directc2c_config directc2c_cfg = PICA_DIRECTC2C_CFG_DISABLED;
struct PICA_listener *l = NULL;


int _outgoing_chnl = 0;

int connected_to_node = 0;
int c2c_active = 0;
int c2c_in_progress = 0;

int echo_mode = 0;
int random_message_mode = 0;
int delivery_acks_count = 0;
int rmm_wait_for_reply = 0;

unsigned char buf[64 * 1024];

int accept_cb(const unsigned char *id)
{
	printf("accept_cb: %s\n", PICA_id_to_base64(id, NULL));
	return 1;
}

//получение сообщения.
void newmsg_cb(const unsigned char *peer_id, const char* msgbuf, unsigned int nb, int type)
{

	if (echo_mode)
	{
		int ret;

		ret = PICA_send_msg(chn, msgbuf, nb);

		if (ret != PICA_OK)
			printf("failed to send message, error = %i\n", ret);

		return;
	}

	if (random_message_mode)
	{
		//compare received & sent messages, fail if they do not match exactly
		if (memcmp(msgbuf, buf, nb) != 0)
		{
			printf("sent and returned messages are not equal!\n");
			exit(1);
		}

		rmm_wait_for_reply = 0;
	}

	memcpy(buf, msgbuf, nb);
	buf[nb] = 0;
	printf(" %s: ", PICA_id_to_base64(peer_id, NULL));
	puts(buf);
}
//получение подтверждения о доставке сообщения
void msgok_cb(const unsigned char *peer_id)
{
	delivery_acks_count++;
	puts("[V]");
}
//создание канала с собеседником
void c2c_established_cb(const unsigned char *peer_id)
{
	c2c_active = 1;
	c2c_in_progress = 0;
	puts("c2c_established_cb");
	chn = c->chan_list_head;//hack, its better to pass new c2c pointer as callback arg
}

void c2c_failed_cb(const unsigned char *peer_id)
{
	c2c_in_progress = 0;
	printf("Failed to create c2c to %s\n", PICA_id_to_base64(peer_id, NULL));
	chn = 0;
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
	c2c_active = 0;
	c2c_in_progress = 0;
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
	printf("peer's certificate (%s): \n%.*s\n", PICA_id_to_base64(peer_id, NULL), nb, cert_pem);
	return 1;
}

int accept_file_cb(const unsigned char  *peer_id, uint64_t  file_size, const char *filename, unsigned int filename_size)
{
	int ret;
	char namebuf[256];

	printf("accept_file_cb: peer_id %s filename: %.*s\n", PICA_id_to_base64(peer_id, NULL), filename_size, filename);

	sprintf(namebuf, "/tmp/received_%.*s", filename_size, filename);

	ret = PICA_accept_file(chn, namebuf, strlen(namebuf));

	if (ret != PICA_OK)
	{
		printf("faild to accept file, errror %i\n", ret);
		return 0;
	}

	return 2;
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
	connected_to_node = 1;
	printf("c2n_established_cb: %p\n my_id %s\n", c2n, PICA_id_to_base64(c2n->acc->id, NULL));
}

void c2n_failed_cb(struct PICA_c2n *c2n, int error)
{
	printf("c2n_failed_cb: %p error_code %i\n", c2n, error);
	c = NULL;
}

void c2n_closed_cb(struct PICA_c2n *c2n, int error)
{
	printf("c2n_closed_cb: %p error_code %i\n", c2n, error);
	c = NULL;
}

void listener_error(struct PICA_listener *lst, int errorcode)
{
	printf("listener_error: %p error code %i\n", lst, errorcode);
	l = NULL;
}

void multilogin_cb(time_t timestamp, void *addr_bin, const char *addr_str, uint16_t port)
{
	printf("received MULTILOGIN\n");
}

struct PICA_client_callbacks cbs =
{
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
	listener_error,
	multilogin_cb
};

int main(int argc, char** argv)
{
	int ret, opt;
	unsigned char peer_id[PICA_ID_SIZE];
	struct PICA_c2n* c2ns[] = {NULL, NULL};

	fd_set stdinfds;
	struct timeval tv;


	const char *nodeaddr = NULL, *nodeport = NULL,
		   *certfile = NULL, *peerid = NULL,
		   *localdirectaddr = NULL, *extport = NULL,
		   *filetosend = NULL,
		   *locport = NULL;

	int multilogin = PICA_MULTILOGIN_PROHIBIT;

	while ((opt = getopt(argc, argv, "a:p:c:i:d:e:l:f:tmrM:")) != -1)
	{
		switch(opt)
		{
		case 'a':
			nodeaddr = optarg;
		break;

		case 'p':
			nodeport = optarg;
		break;

		case 'c':
			certfile = optarg;
		break;

		case 'i':
			peerid = optarg;
		break;

		case 'd':
			localdirectaddr = optarg;
		break;

		case 'e':
			extport = optarg;
		break;

		case 'f':
			filetosend = optarg;
		break;

		case 'l':
			locport = optarg;
		break;

		case 't':
			directc2c_cfg = PICA_DIRECTC2C_CFG_CONNECTONLY;
		break;

		case 'm':
			echo_mode = 1;
		break;

		case 'r':
			random_message_mode = 1;
		break;

		case 'M':
			if (strcmp(optarg, "prohibit") == 0)
				multilogin = PICA_MULTILOGIN_PROHIBIT;
			else if (strcmp(optarg, "replace") == 0)
				multilogin = PICA_MULTILOGIN_REPLACE;
			else if (strcmp(optarg, "sync") == 0)
				multilogin = PICA_MULTILOGIN_ALLOW;
			else
			{
				fprintf(stderr, "invalid multilogin option argument: %s\n", optarg);
				return -1;
			}
		break;

		default:
			fprintf(stderr, "invalid arg %s\n", optarg);
			return -1;
		}
	}

	if (echo_mode && random_message_mode)
	{
		puts("echo mode and random test messages mode are mutually exclusive, please select only one of them");
		return -1;
	}

	if (argc < 4 || !nodeaddr || !nodeport || !certfile)
	{
		puts("usage: test_client -a node address -p node port -c cert_filename  [-i peer id]\n\
		-d local address for direct incoming c2c connections\n\
		-e external port for incoming direct connections\n\
		-l internal port to listen for incoming direct connections\n\
		-t enable connect-only mode for direct c2c connections\n\
		-m start in echo mode, received messages are sent back, input from stdin is not accepted\n\
		-r send random test messages and exit, expects other side to be run in echo mode\n\
		-M multilogin policy, one from \"prohibit\", \"replace\", \"sync\"\n\
cert_filename should point to file that contains client certificate, private key and Diffie-Hellman parameters in PEM format");
		return 0;
	}



	puts(SSLeay_version(SSLEAY_VERSION));
	printf("Calling PICA_init...\n");

	PICA_client_init(&cbs);

	printf("opening account...");
	ret = PICA_open_acc(certfile, certfile, certfile, NULL, &acc);

	if (ret != PICA_OK)
	{
		fprintf(stderr, "failed to open account, ret = %i\n", ret);
		return 1;
	}

	if (localdirectaddr)
	{
		int intp = 2300, extp = 2300;

		directc2c_cfg = PICA_DIRECTC2C_CFG_ALLOWINCOMING;

		if (extport)
			extp = atoi(extport);
		if (locport)
			intp = atoi(locport);

		printf("new listener on %s port %i external announced port %i\n", localdirectaddr, intp, extp);

		ret = PICA_new_listener(acc, localdirectaddr, extp, intp, &l);

		if (ret != PICA_OK)
		{
			fprintf(stderr, "failed to create listener, error code: %i\n", ret);
			l = NULL;
		}

	}

	printf("making connection...\n");

//PICA_new_c2n(const char *nodeaddr, unsigned int port, const char *CA_file, const char *cert_file, const char *pkey_file, const char* password, struct PICA_c2n **ci)
	ret = PICA_new_c2n(acc, nodeaddr, atoi(nodeport), directc2c_cfg, multilogin, l, &c);

	ERR_print_errors_fp(stdout);

	if (ret == PICA_OK)
		printf("PICA_OK started c2n connection to node...\n");
	else
	{
		printf("PICA_new_c2n: ret=%i\n", ret);
		return 1;
	}

	c2ns[0] = c;


	while(PICA_event_loop(c2ns, 500) == PICA_OK && c != NULL)
	{
		//puts("event loop...");
		if (peerid && connected_to_node == 1 && c2c_active == 0 && c2c_in_progress == 0)
		{
			if (PICA_id_from_base64(peerid, peer_id) == NULL)
			{
				puts("invalid peer_id");
				return 1;
			}

			printf("Creating c2c to %s...\n", PICA_id_to_base64(peer_id, NULL));

			ret = PICA_new_c2c(c, peer_id, l, &chn);
			//sleep(17);//timeout test
			if (ret != PICA_OK)
			{
				printf("error: ret=%i\n", ret);
				return 1;
			}

			printf("chn = %p\n", chn);
			c2c_in_progress = 1;

		}

		if (random_message_mode && connected_to_node == 1 && c2c_active == 1 && rmm_wait_for_reply == 0)
		{
			static int test_stage = 0;

			switch(test_stage)
			{
			//send zero-length message
			case 0:
				ret = PICA_send_msg(chn, NULL, 0);
				test_stage++;
				rmm_wait_for_reply = 1;
				break;

			//send max length message
			case 1:
				memset(buf, 'X', PICA_PROTO_C2CMSG_MAXDATASIZE);
				ret = PICA_send_msg(chn, buf, PICA_PROTO_C2CMSG_MAXDATASIZE);
				test_stage++;
				rmm_wait_for_reply = 1;
				break;

			//send random length message
			case 2:
				RAND_pseudo_bytes((unsigned char*)&ret, sizeof ret);
				if (ret < 0)
					ret = 0 - ret;
				ret %= PICA_PROTO_C2CMSG_MAXDATASIZE;
				memset(buf, 'x', ret);
				ret = PICA_send_msg(chn, buf, ret);
				test_stage++;
				rmm_wait_for_reply = 1;
				break;

			default:
				puts("waiting for test messages to be delivered");
			}

			if (ret != PICA_OK)
			{
				printf("failed to send message, error = %i\n", ret);
				return 1;
			}

			if (delivery_acks_count == 3)
				break;//exit main loop on success
		}

		if (connected_to_node == 1 && c2c_active == 1 && !echo_mode && !random_message_mode)
		{
			FD_ZERO(&stdinfds);
			FD_SET(STDIN_FILENO, &stdinfds);

			tv.tv_sec = 0;
			tv.tv_usec = 500000;

			ret = select(STDIN_FILENO + 1, &stdinfds, NULL, NULL, &tv);

			if (ret == -1)
			{
				perror("select() on stdin failed");
				return 1;
			}

			if (ret > 0 && FD_ISSET(STDIN_FILENO, &stdinfds))
			{
				ret = read(STDIN_FILENO, buf, 256);

				if (ret <= 0)
					{
						perror("read from stdin");
						return 1;
					}

				ret = PICA_send_msg(chn, buf, ret);

				if (ret != PICA_OK)
					printf("failed to send message, error = %i\n", ret);
			}
		}

		if (connected_to_node == 1 && c2c_active == 1 && filetosend)
		{
			printf("sending file %s to peer...\n", filetosend);
			ret = PICA_send_file(chn, filetosend);

			if (ret != PICA_OK)
				printf("failed to send file %s, error = %i\n", filetosend, ret);

			filetosend = NULL;
		}


	}



	if (c)
		PICA_close_c2n(c);

	if(l)
		PICA_close_listener(l);

	return 0;
}
