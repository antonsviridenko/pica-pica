

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
#include <signal.h>

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

#define CHILD_TIMEOUT 10

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

int readCSRname(string csr_filename, char *namebuf)
{
char pbuf[512];
unsigned int bytes_read;
FILE *p;
int ret = 1;
char *ptr1, *ptr2;

p = popen(("openssl req -subject -nameopt oneline,-esc_msb,utf8 -noout -in " + csr_filename) .c_str(), "r");
if (!p)
    return 1;

bytes_read = fread(pbuf, 1, 511, p);

if (bytes_read < 15)
    goto retn;

pbuf[bytes_read] = '\0';

puts(pbuf);//debug

ptr1 = strstr(pbuf, "CN = ");

if (!ptr1)
    goto retn;

puts("1");//debug

ptr2 = ptr1+ 5;//skip "CN = "

ptr1 = strchr(ptr2, '\n');

if (!ptr1)
    goto retn;

puts("3");//debug

*ptr1 = '\0';


ptr1 = ptr2;

while(*ptr1)
{
    switch(*ptr1)
    {
	case '#':
	case '$':
	case '&':
	case '\"':
	case '\'':
	case '=':
	case '(':
	case ')':
	case '\\':
	case '/':
	case '|':
	case '`':
	case '!':
	case '<':
	case '>':
	case '{':
	case '}':
	case '[':
	case ']':
	case '+':
	    goto retn;
    }    
    ptr1++;
}

strncpy(namebuf, ptr2, 52);

printf("name is %s\n", namebuf);//debug

ret = 0;

retn:
pclose(p);
return ret;
}

int main(int argc, char *argv[])
{
int s, client_socket, ret, pos, disconnect_flag, bytes_read, cert_size;
struct sockaddr_in sd, sc;
char namebuf[64];


s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

memset(&sd,0,sizeof(sd));

sd.sin_family = AF_INET;
sd.sin_addr.s_addr = INADDR_ANY;
sd.sin_port = htons(PICA_REGISTRAR_PORT);

ret = bind(s,(struct sockaddr*)&sd,sizeof(sd));

ret = listen(s, 60);

signal(SIGCHLD, SIG_IGN);

while (1)
	{
	 socklen_t sockaddr_size = sizeof(sc);
	 pid_t pid;
	    
	memset(&sc, 0, sizeof(sc));

	client_socket = accept(s, (struct sockaddr*)&sc,  &sockaddr_size);

	if (client_socket >= 0)
		{
		    
		if (IP_ban(&sc))
			{
			close(client_socket);
			continue;
			}
			
		
		pid = fork();
		
		if (pid > 0)
		{
		    	current_id++;
			baninfo_update(&sc);
			close(client_socket);
		    	continue;
		}
		
		if (pid == -1)
		{
		    perror("fork() failed");
		    continue;
		}
		//now we are in the child process		
		alarm(CHILD_TIMEOUT);
	
		disconnect_flag = read_CSR(client_socket, &bytes_read);
		
		if (disconnect_flag)
			{
			shutdown(client_socket, SHUT_RDWR);
			close(client_socket);
		    	exit(1);
			}
		
		
		std::string filename = (string("/tmp/CSR-") + inet_ntoa(sc.sin_addr) );
		
		if (store_CSR(filename, bytes_read))
			{
			shutdown(client_socket, SHUT_RDWR);
			close(client_socket);
		    	exit(1);
			}
			
		//get name from CSR
		memset(namebuf, 0, 64);
		
		if (readCSRname(filename, namebuf))
		{
		    printf("invalid name!!!");
		    close(client_socket);
		    exit(1);
		}
			
		//prepare ca environment
		std::string ca_dir;
		{
		    char pidstr[16];
		    sprintf(pidstr, "%u", getpid());
		    FILE *f;
		    
		    ca_dir = (string)"/tmp/ca_dir." + pidstr;
		    if (-1 == mkdir(ca_dir.c_str(), S_IRUSR | S_IWUSR | S_IXUSR))  
		    {
		       	perror("mkdir() failed");
			close(client_socket);
			remove(filename.c_str());
			exit(1);
		    }
		    
		    if (-1 == chdir(ca_dir.c_str()))
		    {
		       	perror("chdir() failed");
			close(client_socket);
			remove(filename.c_str());
			rmdir(ca_dir.c_str());
			exit(1);
		    }
		    
		    f = fopen((ca_dir + '/' + "serial").c_str(), "w");
		    
		    if (!f)
		    {
			perror("unable to create serial file");
			close(client_socket);
			remove(filename.c_str());
			rmdir(ca_dir.c_str());
			exit(1);
		    }
		    
		    fprintf(f, "%X", current_id);
		    fclose(f);
		    
		    f = fopen((ca_dir + '/' + "database").c_str(), "w");
		    
		    if (!f)
		    {
			perror("unable to create database file");
			close(client_socket);
			remove(filename.c_str());
			rmdir(ca_dir.c_str());
			exit(1);
		    }
		    
		    fclose(f);		    
		}
		
		char current_id_str[256];
		
		sprintf(current_id_str, "%u", current_id);
		
		std::string cert_filename = (string)(current_id_str) + ".pem";
		
		std::string sign_command = (string)"openssl ca -config /home/root_jr/temp/catest/ca_config.txt -utf8  -subj \"/CN=" + current_id_str + "#" + namebuf +  "\" -batch -notext -out " + cert_filename + " -in " + filename;
		
		puts(sign_command.c_str());//debug
		
		system(sign_command.c_str());
		
		if (send_cert(client_socket, cert_filename))
		    goto freeres_1;
		
		
		freeres_1:
		chdir("/");
		system(((string)"rm -rf " + ca_dir).c_str());
		remove(cert_filename.c_str());
		
		freeres_2:
		remove(filename.c_str());
		
		freeres_3:
		shutdown(client_socket, SHUT_RDWR);
		close(client_socket);
		
		exit(0);//exiting from the child process
		}
	else
		perror("accept");

	}


return 0;
}