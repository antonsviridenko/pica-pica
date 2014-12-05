 
#ifndef PICA_NODECONFIG_H
#define PICA_NODECONFIG_H

#ifndef PICA_INSTALLPREFIX
#define PICA_INSTALLPREFIX "/usr"
#endif

#ifndef PICA_SYSCONFDIR
#define PICA_SYSCONFDIR "/etc"
#endif 

#ifndef PICA_LOCALSTATEDIR
#define PICA_LOCALSTATEDIR "/var"
#endif

#define PICA_NODECONFIG_DEF_ANNOUNCED_ADDR "0.0.0.0"
#define PICA_NODECONFIG_DEF_LISTEN_PORT "2299"
#define PICA_NODECONFIG_DEF_NODES_DB_FILE PICA_LOCALSTATEDIR"/lib/pica-node/nodelist.db"
#define PICA_NODECONFIG_DEF_CONFIG_FILE PICA_SYSCONFDIR"/pica-node.conf"
#define PICA_NODECONFIG_DEF_CA_CERT_FILE PICA_INSTALLPREFIX"/share/pica-node/CA.pem"

struct nodeconfig
{
char			*announced_addr;
char 			*listen_port; 
char 			*nodes_db_file;
char			*config_file;
char			*CA_cert_file;
int 			disable_reserved_addrs;
//int			conn_speed_limit;
};

extern struct nodeconfig nodecfg;

int PICA_nodeconfig_load(const char *config_file);

#endif