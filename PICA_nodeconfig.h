 
#ifndef PICA_NODECONFIG_H
#define PICA_NODECONFIG_H

//FIXME these default values should be set by "configure" script
//FIXME put these macros inside man pages and substitute them using sed during "make install"
#define PICA_NODECONFIG_DEF_ANNOUNCED_ADDR "0.0.0.0"
#define PICA_NODECONFIG_DEF_LISTEN_PORT 2299
#define PICA_NODECONFIG_DEF_NODES_DB_FILE "/var/lib/pica-node/nodelist.db"
#define PICA_NODECONFIG_DEF_CONFIG_FILE "/etc/pica-node.conf"
#define PICA_NODECONFIG_DEF_CA_CERT_FILE "/usr/share/pica-node/CA.pem"

struct nodeconfig
{
char			*announced_addr;
char 			*listen_port; 
char 			*nodes_db_file;
char			*config_file;
char			*CA_cert_file;
};

extern struct nodeconfig nodecfg;

int PICA_nodeconfig_load(const char *config_file);

#endif