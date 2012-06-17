#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

int get_id_fromsubjstr(char* DN_str,unsigned int* id)
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

int main()
{
	int ret;unsigned int id;
	char dnstr[]="/O=TESTNET/OU=CA/CN=#\xD1\x82\xD0\xB5\xD1\x81\xD1\x82\xD0\xB5\xD1\x80";
	
	ret=get_id_fromsubjstr(dnstr,&id);
	printf("dnstr=%s, ret=%i, id=%u \n",dnstr,ret,id);
	
return 0;
}
