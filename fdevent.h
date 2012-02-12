#ifndef __LOONG_SSO_FDEVENT_H__
#define __LOONG_SSO_FDEVENT_H__


typedef enum 
{
	FDEVENT_READ,
	FDEVENT_WRITE,
	FDEVENT_ERROR
} fdevent_t;


#ifdef USE_EPOLL

#include <sys/epoll.h>

struct fdevent
{
	int                fd;
	uint32_t           maxfd;
	struct epoll_event *events;

	void (* watch)();
	void (* destroy)(); 
	int  (* delete)(int fd, void *data);
	int  (* change)(int fd, void *data, fdevent_t filter);
	int  (* insert)(int fd, void *data, fdevent_t filter);
};

#endif


struct fdevent *fdevent_init(uint32_t maxfd);



#endif

