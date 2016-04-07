#include <limits.h>
#include <string.h>
#include "PICA_proto.h"
#include "PICA_msgproc.h"


//возвращает размер сообщения либо UINT_MAX,если количество прочитанных байт недостаточно для определения размера
//либо 0 при ошибочных данных
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

//Определяет тип сообщения размера size, находящегося в буфере buf, вызывает соответствующий обработчик и передает обработчику аргумент arg. Возвращает значение, которое вернула функция-обработчик сообщения. Если тип сообщения неизвестен, возвращает 0
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

		memmove(buf, buf + uret, *read_pos); //сдвиг содержимого буфера к началу
	}

	return 1;
}
