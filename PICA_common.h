#ifndef PICA_COMMON_H
#define PICA_COMMON_H

#ifdef WIN32
#define CLOSE closesocket
#define SHUTDOWN(s) shutdown(s,SD_BOTH)
#define IOCTLSETNONBLOCKINGSOCKET(s, arg) ioctlsocket(s, FIONBIO, (unsigned long*)(arg))
#else
#define CLOSE close
#define SHUTDOWN(s) shutdown(s,SHUT_RDWR)
#define IOCTLSETNONBLOCKINGSOCKET(s, arg) ioctl(s, FIONBIO, (int*)(arg))
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
