#include "../PICA_nodeaddrlist.h"
#include <stdio.h>


struct PICA_nodeaddr *head=0,*end=0;

int main(int argc,char* argv[])
{
unsigned int N,ret;
if (argc==1)
	{
	puts("test_nodeaddrlist load_list_filename [save_list_filename]");	
	}
else
	{
	puts(argv[1]);
	N=PICA_nodeaddrlist_load(argv[1],&head,&end);
	printf("N=%u\n",N);
	
	if (argc==3)
		{
		ret=PICA_nodeaddrlist_save(argv[2],head);
		printf("save=%i to %s \n",ret,argv[2]);
		}
	}

return 0;
}
