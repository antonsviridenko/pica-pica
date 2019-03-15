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

#ifndef PICA_NODECONFIG_H
#define PICA_NODECONFIG_H

#ifndef PICA_INSTALLPREFIX
#define PICA_INSTALLPREFIX "/usr"
#endif

#ifndef PICA_SYSCONFDIR
#define PICA_SYSCONFDIR "/etc"
#endif

#ifndef PICA_LOCALSTATEDIR
#define PICA_LOCALSTATEDIR "/var/lib"
#endif

#define PICA_NODECONFIG_DEF_ANNOUNCED_ADDR "autoconfigure"
#define PICA_NODECONFIG_DEF_LISTEN_PORT "2299"

#ifndef WIN32

#define PICA_NODECONFIG_DEF_NODES_DB_FILE PICA_LOCALSTATEDIR"/pica-node/nodelist.db"
#define PICA_NODECONFIG_DEF_CONFIG_FILE PICA_SYSCONFDIR"/pica-node.conf"
#define PICA_NODECONFIG_DEF_DH_PARAM_FILE PICA_INSTALLPREFIX"/share/pica-node/dhparam4096.pem"

#else

#define PICA_NODECONFIG_DEF_NODES_DB_FILE "nodelist.db"
#define PICA_NODECONFIG_DEF_CONFIG_FILE "pica-node.conf"
#define PICA_NODECONFIG_DEF_DH_PARAM_FILE "share\\dhparam4096.pem"

#endif

struct nodeconfig
{
	char			*announced_addr;
	char 			*listen_port;
	char 			*nodes_db_file;
	char			*config_file;
	char			*dh_param_file;
	int 			disable_reserved_addrs;
//int			conn_speed_limit;
};

extern struct nodeconfig nodecfg;

int PICA_nodeconfig_load(const char *config_file);

#endif
