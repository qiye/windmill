#include "server.h"

void timer_insert(struct request *req)
{
	LL_TAIL(&srv.timer, &req->entry);
}

void timer_remove(struct request *req)
{
	LL_DEL(&req->entry);
}

void timer_check()
{
	time_t         now;
	struct request *req;
	struct llhead  *lp, *tmp;
	
	now = time((time_t*)0);
	LL_FOREACH_SAFE(&srv.timer, lp, tmp) 
	{
		req = LL_ENTRY(lp, struct request, entry);
		if(now > (req->now+srv.timeout))
		{
			request_delete(req->fd, req);
			continue;
		}

		break;
	}
}
