

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <time.h>

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>

using namespace std;

#define PICA_REGISTRAR_PORT 2299
#define READ_BUF_SIZE 4096
#define WRITE_BUF_SIZE 8192

#define BAN_THRESHOLD_SECS 60

char read_buf[READ_BUF_SIZE];

char write_buf[WRITE_BUF_SIZE];

struct ban_info
{
    unsigned int reg_count;
    time_t last_reg;
};

static std::map<in_addr_t, struct ban_info> banmap;


unsigned int current_id = 30000;

int read_CSR(int client_socket, int *bytes_read)
{
int pos = 0, ret;
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

int store_CSR(string csr_filename, int csr_size)
{
FILE *csr_temp;
int ret;

csr_temp = fopen(csr_filename.c_str(), "w");

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
	remove(csr_filename.c_str());
	return 1;
}

fclose(csr_temp);

return 0;
}

int send_cert(int client_socket, string cert_filename)
{
int cert_size, ret, bs;
FILE *cert;

 //get file size
struct stat st;
if (stat(cert_filename.c_str(), &st))
	return 1;
			
cert_size = st.st_size;
			
		
cert = fopen(cert_filename.c_str(), "r");
		
if (!cert)
	{
	    perror("fopen");
	    return 1;
	}

//read
ret = fread(write_buf, cert_size, 1, cert);

if (ret !=1)
	{
	 perror("fread");
	 fclose(cert);
	 return 1;
	}
	
//send   

bs = 0;
do
{
    ret = send(client_socket, write_buf + bs, cert_size - bs, MSG_NOSIGNAL);
    
    if (ret <=0)
	break;
    
    bs += ret;
    
} while (bs < cert_size);

if (bs < cert_size)
    return 1;

return 0;
}

//check if we should ban this IP
int IP_ban(struct sockaddr_in *a)
{
    time_t time_now =time(NULL);
    
    if (banmap[a->sin_addr.s_addr].reg_count >=3 && (time_now - banmap[a->sin_addr.s_addr].last_reg ) < BAN_THRESHOLD_SECS)
    {
	banmap[a->sin_addr.s_addr].reg_count++;
	printf("IP %.16s temporarily banned\n", inet_ntoa(a->sin_addr));
	return 1;
    }

return 0;
}

//add successfully registered client to ban info
void baninfo_update(struct sockaddr_in *a)
{
    time_t time_now =time(NULL);
    
    if ((time_now - banmap[a->sin_addr.s_addr].last_reg ) > BAN_THRESHOLD_SECS)
	banmap[a->sin_addr.s_addr].reg_count = 0;
    
    
    banmap[a->sin_addr.s_addr].reg_count++;
    
    banmap[a->sin_addr.s_addr].last_reg = time_now;
}

int main(int argc, char *argv[])
{
int s, client_socket, ret, pos, disconnect_flag, bytes_read, cert_size;
struct sockaddr_in sd, sc;


s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

memset(&sd,0,sizeof(sd));

sd.sin_family = AF_INET;
sd.sin_addr.s_addr = INADDR_ANY;
sd.sin_port = htons(PICA_REGISTRAR_PORT);

ret = bind(s,(struct sockaddr*)&sd,sizeof(sd));

ret = listen(s, 60);

while (1)
	{
	 socklen_t sockaddr_size = sizeof(sc);
	    
	memset(&sc, 0, sizeof(sc));

	client_socket = accept(s, (struct sockaddr*)&sc,  &sockaddr_size);

	if (client_socket >= 0)
		{
		    
		if (IP_ban(&sc))
			{
			close(client_socket);
			continue;
			}
		
		disconnect_flag = read_CSR(client_socket, &bytes_read);
		
		if (disconnect_flag)
			{
			shutdown(client_socket, SHUT_RDWR);
			close(client_socket);
		    	continue;
			}
		
		
		
		
		std::string filename = (string("/tmp/CSR-") + inet_ntoa(sc.sin_addr) );
		
		if (store_CSR(filename, bytes_read))
			{
			shutdown(client_socket, SHUT_RDWR);
			close(client_socket);
		    	continue;
			}
		
		char current_id_str[256];
		
		sprintf(current_id_str, "%u", current_id);
		
		std::string cert_filename = (string)(current_id_str) + ".pem";
		
		std::string sign_command = (string)"openssl ca -config ca_config.txt  -utf8 -subj /CN=" + current_id_str + "\\#tester -batch -notext -out " + cert_filename + " -in " + filename;
		
		puts(sign_command.c_str());//debug
		
		system(sign_command.c_str());
		
		if (send_cert(client_socket, cert_filename))
		    goto freeres_1;
		
		
		current_id++;
		baninfo_update(&sc);
		
		
		freeres_1:
		remove(cert_filename.c_str());
		
		freeres_2:
		remove(filename.c_str());
		
		freeres_3:
		shutdown(client_socket, SHUT_RDWR);
		close(client_socket);
		
		
		}
	else
		perror("accept");

	}


return 0;
}