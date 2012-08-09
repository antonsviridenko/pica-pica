bin_PROGRAMS = pica-node

pica_node_SOURCES = PICA_node.c PICA_msgproc.c PICA_nodeaddrlist.c \
		PICA_nodejoinskynet.c PICA_nodeconfig.c PICA_log.c \
		iniparser/src/dictionary.c iniparser/src/iniparser.c \
		PICA_node.h PICA_msgproc.h PICA_nodeaddrlist.h \
		PICA_nodejoinskynet.h PICA_nodeconfig.h PICA_log.h \
		iniparser/src/dictionary.h iniparser/src/iniparser.h \
		PICA_common.h PICA_proto.h
		
dist_man_MANS = pica-node.8 pica-node.conf.5

pica_node_CPPFLAGS = -DPICA_PREFIX=\"$(prefix)\"

dist_pkgdata_DATA = CA.pem


