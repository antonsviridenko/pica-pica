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

#ifndef PICA_MSGPROC_H
#define PICA_MSGPROC_H

#define PICA_MSG_FIXED_SIZE 0
#define PICA_MSG_VAR_SIZE 1
#define PICA_MSG_VARSIZE_INT8 1
#define PICA_MSG_VARSIZE_INT16 2

struct PICA_msginfo
{
	unsigned int msgid;
	/* PICA_MSG_FIXED_SIZE if message has fixed size, PICA_MSG_VAR_SIZE in case of variable size */
	unsigned int fixedsz;
	/* Size of the whole message in case of fixed-size message or number of octets that store size of the message */
	unsigned int sz;
	/* Pointer to message handler */
	unsigned int (*msgprocfn)(unsigned char*, unsigned int, void*);
};

unsigned int PICA_msggetsize(unsigned char *buf, unsigned int nb, struct PICA_msginfo *msgs, unsigned int nmsg);

unsigned int PICA_processmsg(unsigned char *buf, unsigned int size, void* arg, struct PICA_msginfo *msgs, unsigned int nmsg);

unsigned int PICA_processdatastream(unsigned char *buf, unsigned int *read_pos, void* arg, const struct PICA_msginfo *msgs, unsigned int nmsg);
#endif
