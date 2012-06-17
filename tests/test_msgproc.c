#include <stdio.h>

#include "../PICA_msgproc.h"

 unsigned int _PICA_msginfo_arr_size=1;

 struct PICA_msginfo  _PICA_msginfo_arr[1]={0xCF,PICA_MSG_VAR_SIZE,3};

char tstpck[12]={0xCF,0xCF,0x03,0x01,1,0,0,0,0,0,0,0};

int main()
{
printf("nb=1 ret=%u\n",PICA_msggetsize(tstpck,1));
printf("nb=4 ret=%u\n",PICA_msggetsize(tstpck,4));
printf("nb=12 ret=%X\n",PICA_msggetsize(tstpck,12));	

return 0;
}