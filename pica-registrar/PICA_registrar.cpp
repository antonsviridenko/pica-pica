

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
#include <cstdlib>
#include <map>

using namespace std;

#define PICA_REGISTRAR_PORT 2299
#define READ_BUF_SIZE 4096
#define WRITE_BUF_SIZE 8192

char read_buf[READ_BUF_SIZE];

char write_buf[WRITE_BUF_SIZE];


unsigned int current_id = 30 000;

int read_CSR(int client_socket, int *bytes_read)
{
int pos = 0;
int disconnect_flag = 0;
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
			 
			 *bytes_read = pos;
			}
		while(pos < READ_BUF_SIZE && !disconnect_flag && !std::strstr(read_buf, "-----END CERTIFICATE REQUEST-----"));

return disconnect_flag;
}

int store_CSR(std::string csr_filename, int csr_size)
{
FILE *csr_temp;

csr_temp = fopen(csr_filename.c_str, "w");

if (!csr_temp)
	{
	perror("fopen");
	return 1;
	}

ret = fwrite(read_buf, csr_size, 1, csr_temp);

if (ret != 1)
{
	perror("fwrite");
	fclose(csr_temp);
	remove(csr_filename.c_str);
	return 1;
}

fclose(csr_temp);

return 0;
}

int main(int argc, char *argv[])
{
int socket, client_socket, ret, pos, disconnect_flag, bytes_read, cert_size;
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
		
		disconnect_flag = read_CSR(client_socket, &bytes_read);
		
		if (disconnect_flag)
			continue;
		
		{
		FILE *cert;
		
		std::string filename = (string("/tmp/CSR-") + inet_ntoa(sc.sin_addr) );
		
		if (store_CSR(filename, bytes_read))
			continue;
		
		std::string cert_filename = atoi(current_id) + ".pem";
		
		std::string sign_command = "openssl ca -config ca_config.txt  -utf8 -subj /CN=" + atoi(current_id) + "\\#tester -batch -notext -out " + cert_filename + " -in " + csr_temp;
		
		puts(sign_command.c_str);//debug
		
		system(sign_command.c_str);
		
		//get file size
			{
			struct stat st;
			if (stat(cert_filename.c_str(), &st))
				continue;
			
			cert_size = st.st_size;
			}
		
		cert = fopen(cert_filename, "r");
		
		if (!cert)
			{
			 perror("fopen");
			 remove(filename.c_str);
			 remove(cert_filename);
			 continue;
			}
		
		//send
		
		
		current_id++;
		
		remove(filename.c_str);
		}
		
		}
	else
		perror("accept");

	}


return 0;
}