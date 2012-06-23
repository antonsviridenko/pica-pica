 
#ifndef PICA_NODECONFIG_H
#define PICA_NODECONFIG_H

struct nodeconfig
{
char			*announced_addr;
char 			*listen_port; 
char 			*nodes_db_file;
char			*config_file;
};

extern struct nodeconfig nodecfg;

int PICA_nodeconfig_load(const char *config_file);

#endif