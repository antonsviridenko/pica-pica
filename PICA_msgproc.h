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
	unsigned int (*msgprocfn)(unsigned char*, unsigned int, void*); //функция-обработчик данного типа сообщений
};

unsigned int PICA_msggetsize(unsigned char *buf, unsigned int nb, struct PICA_msginfo *msgs, unsigned int nmsg);

unsigned int PICA_processmsg(unsigned char *buf, unsigned int size, void* arg, struct PICA_msginfo *msgs, unsigned int nmsg);

unsigned int PICA_processdatastream(unsigned char *buf, unsigned int *read_pos, void* arg, const struct PICA_msginfo *msgs, unsigned int nmsg);
#endif
