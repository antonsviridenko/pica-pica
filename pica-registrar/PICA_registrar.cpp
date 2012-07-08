

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/errno.h>

#include <map>

#define PICA_REGISTRAR_PORT 2299
#define READ_BUF_SIZE 4096
char read_buf[READ_BUF_SIZE];

int main(int argc, char *argv[])
{
int socket, client_socket, ret, pos, disconnect_flag;
struct sockaddr_in sd, sc;


socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

memset(&sd,0,sizeof(sd));

sd.sin_family = AF_INET;
sd.sin_addr.s_addr = INADDR_ANY;
sd.sin_port = htons(PICA_REGISTRAR_PORT);

ret = bind(socket,(struct sockaddr*)&sd,sizeof(sd));

ret = listen(socket, 60);

while (1)
	{
	memset(&sc, 0, sizeof(sc));

	client_socket = accept(socket, (struct sockaddr*)&sc, sizeof(sc) );

	if (client_socket >= 0)
		{
		 pos = 0;
		 do 
			{
			 ret = recv(client_socket, read_buf + pos, READ_BUF_SIZE - pos, 0);
			 
			 if (ret > 0)
				pos += ret;
			 else
			 	{
				disconnect_flag = 1;
			 	if (ret < 0)
					perror("recv");
			 	}
			 
			}
		while();
		}

	}


return 0;
}