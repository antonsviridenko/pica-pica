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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "PICA_nodeaddrlist.h"
#include "PICA_log.h"


struct cb_param
{
	struct PICA_nodeaddr **n_addr;
	int *N;
};

static int nodeaddr_load_sqlite_cb(void *p, int argc, char **argv, char **ColName)
{
	struct cb_param *cp = (struct cb_param*)p;
	struct PICA_nodeaddr *na;

	if (argc != 4)
		return 1;

	na = (struct PICA_nodeaddr *)malloc(sizeof(struct PICA_nodeaddr));

	if (!na)
		return 1;

	PICA_debug1("node address loaded: addr: %s port %s\n", argv[0], argv[1]);//debug

	strncpy(na->addr, argv[0], 255);
	na->addr[255] = '\0';

	na->port = atoi(argv[1]);

	na->inactive_count = atoi(argv[2]);

	na->last_active = atoi(argv[3]);

	na->next = *(cp->n_addr);

	*(cp->n_addr) = na;
	(*(cp->N))++;

	return 0;
}

int PICA_nodeaddr_list_load(const char* dbfilename, struct PICA_nodeaddr **list_head)
{
	int N = 0, ret;
	sqlite3 *db;
	char *zErrMsg = 0;
	const char query[] = "select address, port, inactive_count, last_active  from nodes order by inactive_count desc, last_active asc";
	struct cb_param p = {list_head, &N};


	ret = sqlite3_open_v2(dbfilename, &db, SQLITE_OPEN_READWRITE, NULL);

	if (ret != SQLITE_OK)
	{
		PICA_fatal( "Can't open nodelist database (%s): %s\n", dbfilename, sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_exec(db, query, nodeaddr_load_sqlite_cb, &p, &zErrMsg);

	if( ret != SQLITE_OK )
	{
		PICA_error("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	}

	sqlite3_close(db);
	return N;
}

int PICA_nodeaddr_update(const char* dbfilename, struct PICA_nodeaddr *naddr, int is_alive)
{
	sqlite3 *db;
	sqlite3_stmt *stmt;
	int ret;
	const char q1[] = "update nodes set last_active = strftime('%s','now'), inactive_count = 0 where address = :addr and port = :port";
	const char q2[] = "update nodes set inactive_count = inactive_count + 1 where address = :addr and port = :port";
	char *query;

	query = is_alive ? q1 : q2;

	ret = sqlite3_open(dbfilename, &db);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't open nodelist database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't prepare SQLite statement: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_bind_text(stmt, 1, naddr->addr, -1, SQLITE_STATIC);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't bind address value: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_bind_int(stmt, 2, naddr->port);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't bind port value: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_step(stmt);

	if (ret != SQLITE_DONE)
	{
		PICA_error( "Can't execute query: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -1;
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	return 1;
}

int PICA_nodeaddr_save(const char* dbfilename, struct PICA_nodeaddr *naddr)
{
	sqlite3 *db;
	sqlite3_stmt *stmt;
	int ret;
	const char query[] = "insert into nodes (address, port, last_active, inactive_count) values(:addr, :port, strftime('%s','now'), 0);";

	ret = sqlite3_open(dbfilename, &db);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't open nodelist database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't prepare SQLite statement: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_bind_text(stmt, 1, naddr->addr, -1, SQLITE_STATIC);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't bind address value: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_bind_int(stmt, 2, naddr->port);

	if (ret != SQLITE_OK)
	{
		PICA_error("Can't bind port value: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -1;
	}

	ret = sqlite3_step(stmt);

	if (ret != SQLITE_DONE)
	{
		PICA_error( "Can't execute query: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -1;
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	return 1;
}

void PICA_nodeaddr_list_free(struct PICA_nodeaddr *list_head)
{
	struct PICA_nodeaddr *pp, *pn;
	pp = list_head;

	while(pp)
	{
		pn = pp->next;
		free(pp);
		pp = pn;
	}
}
