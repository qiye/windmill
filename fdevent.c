#include "server.h"


#ifdef USE_EPOLL

static int fdevent_ctl(int fd, int func, void *data, fdevent_t filter) 
{
	struct epoll_event event;
	
	memset(&event, 0, sizeof(struct epoll_event));

	if (filter == FDEVENT_READ)
		event.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET;
	else if (filter == FDEVENT_WRITE)
		event.events = EPOLLOUT | EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET;
	else
		event.events = 0;
	
	event.data.ptr  = data;
	
	return epoll_ctl(srv.el->fd, func, fd, &event);
}

int fdevent_delete(int fd, void *data)
{
	return fdevent_ctl(fd, EPOLL_CTL_DEL, data, 0);
}

int fdevent_change(int fd, void *data, fdevent_t filter)
{
	return fdevent_ctl(fd, EPOLL_CTL_MOD, data, filter);
}

int fdevent_insert(int fd, void *data, fdevent_t filter)
{
	return fdevent_ctl(fd, EPOLL_CTL_ADD, data, filter);
}

void fdevent_destroy() 
{
	zfree(srv.el->events);
	close(srv.el->fd);
}


void fdevent_watch()
{
	int     nfds, i;
	struct  epoll_event  *cevents;
	
	while(!srv.quit)
	{
		nfds = epoll_wait(srv.el->fd, srv.el->events, srv.el->maxfd, -1);
		
		timer_check();
		for (i = 0, cevents = srv.el->events; i < nfds; i++, cevents++)
		{
			struct request *req = (struct request *)cevents->data.ptr;
			if(req == NULL) continue;
			
			//printf("cevents->data.fd = %d\r\n", cevents->data.fd);
			
			if(cevents->events & (EPOLLHUP | EPOLLERR))
				request_delete(req->fd, req);
			else if((req->state == HTTP_SEND) || (req->state == HTTP_RECV) || (req->state == LINK_POOL))
			{
				req->handler(req->fd, req);
			}
			else if(req->link != NULL)
			{
				DEBUG_PRINT("req->fd = %d\r\n", req->fd);
				//DEBUG_PRINT("req->link->fd = %d\r\n", req->link->fd);
				req->handler(req->link->fd, req);
			}
			else
				request_delete(req->fd, req);
		}
	}
}


struct fdevent *fdevent_init(uint32_t maxfd)
{
	struct fdevent *el = NULL;

	el     = (struct fdevent *)zmalloc(sizeof(struct fdevent));
	if(el == NULL) return NULL;

	el->fd = epoll_create(maxfd);
	if(el->fd == -1) return NULL;

	el->events = zmalloc(sizeof(struct epoll_event) * maxfd);
	if(el->events == NULL)
	{
		zfree(el);
		return NULL;
	}
	el->maxfd    = maxfd;
	el->watch    = fdevent_watch;
	el->insert   = fdevent_insert;
	el->change   = fdevent_change;
	el->delete   = fdevent_delete;
	el->destroy  = fdevent_destroy;

	return el;
}

#endif
