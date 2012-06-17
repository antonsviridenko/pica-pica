#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 


#include <stdio.h>

int main()
{
int ret;
char buf[256];


gets(buf);


ret=inet_aton(buf,(struct in_addr*)(buf+128));

printf("inet_aton()=%i\n",ret);

return 0;
}