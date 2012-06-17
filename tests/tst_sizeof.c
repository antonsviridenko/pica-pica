#include <stdio.h>

char str[]={'A',};
int i[]={1,2,3};

struct tsr
{
int len;
char s[256];
} t = {sizeof(str),"string"};

int main()
{
printf("sizeof str: %u  i:%u\n",sizeof("string"),sizeof(i));
return 0;
}
