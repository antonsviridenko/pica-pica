#include <stdio.h>
 #include "../PICA_node.h"

struct newconn* add_newconn(struct newconn *ncs,int *pos);

struct newconn t1[64];


int main()
{
int tpos=0,i;
struct newconn *nc;

printf("    t1=%X\n",t1);

nc=add_newconn(t1,&tpos);
printf("1) nc=%X  tpos=%i\n",nc,tpos);

tpos=0;
t1[0].sck=1;

nc=add_newconn(t1,&tpos);
printf("2) nc=%X  tpos=%i\n",nc,tpos);

tpos=64;
nc=add_newconn(t1,&tpos);
printf("3) nc=%X  tpos=%i\n",nc,tpos);

for (i=0;i<64;i++)
{
t1[i].sck=65536;
t1[i].tmst=100+i;
}

t1[63].tmst=5;

nc=add_newconn(t1,&tpos);
printf("4) nc=%X  tpos=%i\n",nc,tpos);


return 0;
}
