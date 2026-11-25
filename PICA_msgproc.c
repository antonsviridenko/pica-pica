/*
	(c) Copyright  2012 - 2018 Anton Sviridenko
	https://picapica.im

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, version 3.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <limits.h>
#include <string.h>
#include "PICA_proto.h"
#include "PICA_msgproc.h"


//returns the message size, or UINT_MAX if not enough bytes have been read to determine the size,
//or 0 if the data is invalid
unsigned int PICA_msggetsize(unsigned char *buf, unsigned int nb, struct PICA_msginfo *msgs, unsigned int nmsg)
{
	unsigned int i;

	if (nb < 2)
		return UINT_MAX;

	if (buf[0] != buf[1])
		return 0;


	for (i = 0; i < nmsg; i++)
		if (buf[0] == (unsigned char)msgs[i].msgid)
		{
			if (msgs[i].fixedsz == PICA_MSG_FIXED_SIZE)
				return msgs[i].sz;
			else if (msgs[i].fixedsz == PICA_MSG_VAR_SIZE)
			{
				if (nb >= (msgs[i].sz + 2))
				{
					unsigned int tail_len = 0;
					int j;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
					for (j = 0; j < msgs[i].sz; j++)
						((char*)&tail_len)[sizeof(int) - 1 - j] = buf[2 + j];
#else
					for (j = 0; j < msgs[i].sz; j++)
						((char*)&tail_len)[j] = buf[2 + j];
#endif
					return 2 + msgs[i].sz + tail_len;
				}
				else
					return UINT_MAX;
			}
		}

	return 0;
}

//Determines the type of the message of size size located in buffer buf, calls the corresponding handler and passes it the argument arg. Returns the value returned by the message handler. Returns 0 if the message type is unknown
unsigned int PICA_processmsg(unsigned char *buf, unsigned int size, void* arg, struct PICA_msginfo *msgs, unsigned int nmsg)
{
	int i;

	if (buf[0] != buf[1])
		return 0;

	for (i = 0; i < nmsg; i++)
		if (buf[0] == (unsigned char)msgs[i].msgid)
			return msgs[i].msgprocfn(buf, size, arg);

	return 0;
}

unsigned int PICA_processdatastream(unsigned char *buf, unsigned int *read_pos, void* arg, const struct PICA_msginfo *msgs, unsigned int nmsg)
{
	while(*read_pos)
	{
		unsigned int uret;
		uret = PICA_msggetsize(buf, *read_pos, msgs, nmsg);

		if (!uret)
			return 0;

		if (uret > *read_pos)
			break;

		if (!PICA_processmsg(buf, uret, arg, msgs, nmsg))
			return 0;


		*read_pos -= uret;

		memmove(buf, buf + uret, *read_pos); //move the buffer contents to the beginning
	}

	return 1;
}
