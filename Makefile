OBJS= PICA_msgproc.o PICA_nodeaddrlist.o PICA_node.o PICA_nodejoinskynet.o PICA_nodeconfig.o iniparser.o dictionary.o PICA_log.o
CFLAGS= -pedantic -Wall
LIBS= -lssl -lcrypto -lsqlite3

all: pica-node;

pica-node: $(OBJS)
	gcc   $(OBJS) $(LIBS)  -o pica-node # добавить CFLAGS и все такое

PICA_node.o: PICA_node.c PICA_node.h PICA_msgproc.h PICA_nodeaddrlist.h PICA_nodejoinskynet.h PICA_nodeconfig.h PICA_common.h PICA_proto.h PICA_log.h
	gcc $(CFLAGS) -c PICA_node.c

PICA_msgproc.o: PICA_msgproc.c PICA_msgproc.h
	gcc $(CFLAGS) -c PICA_msgproc.c

PICA_nodeaddrlist.o: PICA_nodeaddrlist.c PICA_nodeaddrlist.h PICA_log.h
	gcc $(CFLAGS) -c PICA_nodeaddrlist.c

PICA_nodejoinskynet.o: PICA_nodejoinskynet.c PICA_nodejoinskynet.h PICA_nodeaddrlist.h PICA_common.h PICA_proto.h PICA_msgproc.h PICA_log.h
	gcc $(CFLAGS) -c PICA_nodejoinskynet.c

PICA_nodeconfig.o: PICA_nodeconfig.c PICA_nodeconfig.h iniparser/src/iniparser.h iniparser/src/dictionary.h
	gcc $(CFLAGS) -c PICA_nodeconfig.c

iniparser.o: iniparser/src/iniparser.c
	gcc $(CFLAGS) -c iniparser/src/iniparser.c

dictionary.o: iniparser/src/dictionary.c
	gcc $(CFLAGS) -c iniparser/src/dictionary.c

PICA_log.o: PICA_log.c PICA_log.h
	gcc $(CFLAGS) -c PICA_log.c


