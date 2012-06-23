 
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

nodecfg.announced_addr = strdup(iniparser_getstring(d, "pica-node:announced_addr", NULL));
nodecfg.listen_port = strdup(iniparser_getstring(d, "pica-node:listen_port", NULL));
nodecfg.nodes_db_file = strdup(iniparser_getstring(d, "pica-node:nodes_db_file", NULL));

iniparser_freedict(d);

return 1;
}