

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/errno.h>

#include <cstring>
#include <map>

using namespace std;

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
		 disconnect_flag = 0;
		 do 
			{
			 ret = recv(client_socket, read_buf + pos, READ_BUF_SIZE - 1 - pos, 0);
			 
			 if (ret > 0)
				pos += ret;
			 else
			 	{
				disconnect_flag = 1;
			 	if (ret < 0)
					perror("recv");
			 	}
			 read_buf[pos + 1] = '\0';
			}
		while(pos < READ_BUF_SIZE && !disconnect_flag && !std::strstr(read_buf, "-----END CERTIFICATE REQUEST-----"));
		
		if (disconnect_flag)
			continue;
		
		{
		FILE *csr_temp;
		
		std::string filename = (string("/tmp/CSR-") + inet_ntoa(sc.sin_addr) );
		
		csr_temp = fopen(filename.c_str, "w");
		
		if (!csr_temp)
			{
			perror("fopen");
			continue;
			}
		
		ret = fwrite(read_buf, pos, 1, csr_temp);
		
		if (ret != 1)
		{
			perror("fwrite");
			fclose(csr_temp);
			remove(filename.c_str);
			continue;
		}
		
		fclose(csr_temp);
		
		//system(openssl ca)
		
		remove(filename.c_str);
		}
		
		}
	else
		perror("accept");

	}


return 0;
}