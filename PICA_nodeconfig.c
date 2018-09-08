/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
	nodecfg.dh_param_file = strdup(iniparser_getstring(d, "pica-node:dh_param_file", PICA_NODECONFIG_DEF_DH_PARAM_FILE));
	nodecfg.disable_reserved_addrs = iniparser_getboolean(d, "pica-node:disable_reserved_addrs", 1);

	iniparser_freedict(d);

	return 1;
}
