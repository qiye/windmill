#include "server.h"

bool request_new(int fd, PF *handler, char *ip)
{
	struct request *req = NULL;

	req = &srv.req[fd];

	anetNonBlock(NULL, fd);
	anetTcpNoDelay(NULL,fd);
	
	
	req->fd           = fd;
	req->now          = time((time_t*)0);
	req->buf          = sdsempty();
	req->path         = NULL;
	req->state        = HTTP_RECV;
	req->sentlen      = 0;
	req->link         = NULL;
	req->handler      = handler;
	req->vm           = NULL;
/*
	req->vm           = lua_newthread(srv.L);
	req->ref          = luaL_ref(srv.L, LUA_REGISTRYINDEX);
	lua_pushthread(req->vm);  
	lua_pushlightuserdata(req->vm, req); 
	lua_setglobal(req->vm, "__USERDATA__");
*/

	memset(req->ip, 0, sizeof(req->ip));
	strncpy(req->ip, ip, 15);
	

	
	srv.el->insert(fd, req, FDEVENT_READ);
	
	//添加定时器
	timer_insert(req);
	//srv->total++;
	return true;
}

void request_change(int fd, struct request *req, PF *handler, fdevent_t filter)
{
	req->handler  = handler;

	srv.el->change(fd, req, filter);
}


void request_delete(int fd, struct request *req)
{
	int i;
	
	if(req->vm)
	{
		lua_settop(req->vm, 0); 
		lua_gc(srv.L, LUA_GCCOLLECT, 0);
		luaL_unref(srv.L, LUA_REGISTRYINDEX, req->ref);
	}
	//if(req->vm) lua_close(req->vm);
	srv.el->delete(req->fd, req);
	close(req->fd);
	
	sdsfree(req->buf);
	if(req->state != LINK_POOL)
	{
		timer_remove(req);
	}
	if(req->link != NULL)
	{
		//把连接放回连接池
		linkpool_free(req->link);
	}
	
	//lua_close(req->vm);
	
	zfree(req->path);
	req->fd    = 0;
	req->buf   = NULL;
	req->link  = NULL;
	req->path  = NULL;
	
	//DEBUG_PRINT("lua_gettop = %d\r\n", lua_gettop(req->vm));
	//zfree(req);
	//req = NULL;
}

