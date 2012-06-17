#include "PICA_nodeaddrlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

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

printf("NODE: addr: %s port %s\n", argv[0], argv[1]);//debug

strncpy(na->addr, argv[0], 255);
na->addr[255] = '\0';

na->port = atoi(argv[1]);

na->inactive_count = atoi(argv[2]);

na->last_active = atoi(argv[3]);

na->next = NULL;

*(cp->n_addr) = na;
(*(cp->N))++;

return 0;
}

int PICA_nodeaddr_list_load(char* dbfilename,struct PICA_nodeaddr **list_head)
{
 int N=0, ret;
sqlite3 *db;
char *zErrMsg = 0;
const char query[] = "select address, port, inactive_count, last_active  from nodes order by inactive_count asc, last_active desc";
struct cb_param p = {list_head, &N};


ret = sqlite3_open(dbfilename, &db);

if (ret != SQLITE_OK)
	{
	fprintf(stderr, "Can't open nodelist database: %s\n", sqlite3_errmsg(db));
	sqlite3_close(db);
	return -1;
	}

ret = sqlite3_exec(db, query, nodeaddr_load_sqlite_cb, &p, &zErrMsg);

if( ret != SQLITE_OK )
	{
	fprintf(stderr, "SQL error: %s\n", zErrMsg);
	sqlite3_free(zErrMsg);
	}

sqlite3_close(db);
return N;
}



int PICA_nodeaddr_list_save(char* listfilename,struct PICA_nodeaddr *list_head)
{
/*
FILE *f_list;
struct PICA_nodeaddr *list_item;

f_list=fopen(listfilename,"w");

if (f_list==0)
	{
	puts("error opening node list for save");
	//ERR_CHECK
	return -1; // -1 коды ошибок
	}

list_item=list_head;
while(list_item)
	{
	int ret;
	ret=fprintf(f_list,"%s %u %u\n",list_item->addr,list_item->port,list_item->unavail_count);

	if (ret<0)
		return -1;
	
	list_item=list_item->next;
	}


return fclose(f_list);*/
}

void PICA_nodeaddr_list_free(struct PICA_nodeaddr *list_head)
{
struct PICA_nodeaddr *pp,*pn;
pp=list_head;

while(pp)
	{
	pn=pp->next;
	free(pp);
	pp=pn;
	}
}