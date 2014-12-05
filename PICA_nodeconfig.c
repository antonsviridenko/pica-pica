 
#include "PICA_nodeconfig.h"

#include "iniparser/src/iniparser.h"
#include "iniparser/src/dictionary.h"

#include <string.h>

struct nodeconfig nodecfg;

int PICA_nodeconfig_load(const char *config_file)
{
dictionary *d;

d = iniparser_load(config_file);

if (!d)
	return 0;

nodecfg.announced_addr = strdup(iniparser_getstring(d, "pica-node:announced_addr", PICA_NODECONFIG_DEF_ANNOUNCED_ADDR));
nodecfg.listen_port = strdup(iniparser_getstring(d, "pica-node:listen_port", PICA_NODECONFIG_DEF_LISTEN_PORT));
nodecfg.nodes_db_file = strdup(iniparser_getstring(d, "pica-node:nodes_db_file", PICA_NODECONFIG_DEF_NODES_DB_FILE));
nodecfg.CA_cert_file = strdup(iniparser_getstring(d, "pica-node:CA_cert_file", PICA_NODECONFIG_DEF_CA_CERT_FILE));
nodecfg.disable_reserved_addrs = iniparser_getboolean(d, "pica-node:disable_reserved_addrs", 1);

iniparser_freedict(d);

return 1;
}