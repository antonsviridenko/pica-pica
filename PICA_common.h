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
#ifndef PICA_COMMON_H
#define PICA_COMMON_H

#ifdef WIN32
#define CLOSE closesocket
#define SHUTDOWN(s) shutdown(s,SD_BOTH)
#define IOCTLSETNONBLOCKINGSOCKET(s, a) {unsigned long arg = (a); ioctlsocket(s, FIONBIO, (unsigned long*)&arg); }
#else
#define CLOSE close
#define SHUTDOWN(s) shutdown(s,SHUT_RDWR)
#define IOCTLSETNONBLOCKINGSOCKET(s, a) {int arg = (a); ioctl(s, FIONBIO, (int*)&arg); }
#endif

#if  defined(WIN32) || defined (__APPLE__)
#define MSG_NOSIGNAL 0
#endif


//call func for each linked list item
#define LISTMAP(listname,type,func,arg) \
{struct type *p;\
p= listname  ## _list_head;\
while(p)\
{func(p,arg);p=p->next;}\
}

#define LISTSEARCH(listname,type, item, value,itemptr) \
{\
struct type *p;\
p= listname  ## _list_head;\
while(p)\
	{\
	if (p-> item == value)\
		{\
		itemptr=p;\
		break;\
		}\
	p=p->next;\
	}\
}


#endif
