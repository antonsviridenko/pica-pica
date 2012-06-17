OBJS= PICA_msgproc.o PICA_nodeaddrlist.o PICA_node.o PICA_nodejoinskynet.o
CFLAGS= -pedantic -Wall

all: node;

node: $(OBJS)
	gcc   $(OBJS) -l ssl -l crypto -lsqlite3 -o node # добавить CFLAGS и все такое

PICA_node.o: PICA_node.c PICA_node.h PICA_msgproc.h PICA_nodeaddrlist.h PICA_nodejoinskynet.h
	gcc $(CFLAGS) -c PICA_node.c

PICA_msgproc.o: PICA_msgproc.c PICA_msgproc.h
	gcc $(CFLAGS) -c PICA_msgproc.c

PICA_nodeaddrlist.o: PICA_nodeaddrlist.c PICA_nodeaddrlist.h
	gcc $(CFLAGS) -c PICA_nodeaddrlist.c

PICA_nodejoinskynet.o: PICA_nodejoinskynet.c PICA_nodejoinskynet.h PICA_nodeaddrlist.h
	gcc $(CFLAGS) -c PICA_nodejoinskynet.c


