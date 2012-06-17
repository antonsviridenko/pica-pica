// структуры и функции для обработки сообщений протокола  
//
#ifndef PICA_MSGPROC_H
#define PICA_MSGPROC_H

#define PICA_MSG_FIXED_SIZE 0
#define PICA_MSG_VAR_SIZE 1
#define PICA_MSG_VARSIZE_INT8 1
#define PICA_MSG_VARSIZE_INT16 2

struct PICA_msginfo
{
unsigned int msgid;
unsigned int fixedsz;//фиксированный или переменный размер
unsigned int sz;//фиксированный размер всего пакета либо кол-во байт, определяющих размер хвостовой части пакета
unsigned int (*msgprocfn)(unsigned char*,unsigned int,void*);//функция-обработчик данного типа сообщений
};

unsigned int PICA_msggetsize(unsigned char *buf,unsigned int nb,struct PICA_msginfo *msgs,unsigned int nmsg);

unsigned int PICA_processmsg(unsigned char *buf,unsigned int size,void* arg,struct PICA_msginfo *msgs,unsigned int nmsg);

unsigned int PICA_processdatastream(unsigned char *buf,unsigned int *read_pos,void* arg,struct PICA_msginfo *msgs,unsigned int nmsg);
#endif
