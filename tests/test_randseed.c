#include <stdio.h>
#include <stdlib.h>

#define DEBUG_RAND

#include "..\PICA_rand_seed.h"

int main(int argc, char *argv[])
{
  PICA_rand_seed();
  
  system("PAUSE");	
  return 0;
}
