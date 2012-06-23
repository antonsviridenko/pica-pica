OBJS= PICA_msgproc.o PICA_nodeaddrlist.o PICA_node.o PICA_nodejoinskynet.o PICA_nodeconfig.o iniparser.o dictionary.o
CFLAGS= -pedantic -Wall

all: pica-node;

pica-node: $(OBJS)
	gcc   $(OBJS) -l ssl -l crypto -lsqlite3 -o pica-node # добавить CFLAGS и все такое

PICA_node.o: PICA_node.c PICA_node.h PICA_msgproc.h PICA_nodeaddrlist.h PICA_nodejoinskynet.h PICA_nodeconfig.h
	gcc $(CFLAGS) -c PICA_node.c

PICA_msgproc.o: PICA_msgproc.c PICA_msgproc.h
	gcc $(CFLAGS) -c PICA_msgproc.c

PICA_nodeaddrlist.o: PICA_nodeaddrlist.c PICA_nodeaddrlist.h
	gcc $(CFLAGS) -c PICA_nodeaddrlist.c

PICA_nodejoinskynet.o: PICA_nodejoinskynet.c PICA_nodejoinskynet.h PICA_nodeaddrlist.h
	gcc $(CFLAGS) -c PICA_nodejoinskynet.c

PICA_nodeconfig.o: PICA_nodeconfig.c PICA_nodeconfig.h iniparser/src/iniparser.h iniparser/src/dictionary.h
	gcc $(CFLAGS) -c PICA_nodeconfig.c

iniparser.o: iniparser/src/iniparser.c
	gcc $(CFLAGS) -c iniparser/src/iniparser.c

dictionary.o: iniparser/src/dictionary.c
	gcc $(CFLAGS) -c iniparser/src/dictionary.c



