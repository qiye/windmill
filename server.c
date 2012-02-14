#include "server.h"


//gcc -o redbridge server.c timer.c pool.c zmalloc.c anet.c  fdevent.c  request.c  sds.c hash.c module/redis.c -O2 -DUSE_EPOLL -ldl -I/usr/local/jemalloc/include -I/usr/local/lua/include /usr/local/lua/lib/liblua.a  -lm /usr/local/jemalloc/lib/libjemalloc.a -lpthread -g -DDEBUG -I./


void sockio_read(int fd, struct request *req);

void sockio_write(int fd, struct request *req);


static int sig_handle(int sig, void (*sigcall)(int))
{
	struct sigaction act;
	
	act.sa_handler = sigcall;
	act.sa_flags = SA_RESTART;
	sigemptyset(&act.sa_mask);

	return sigaction(sig, &act, NULL);
}


int strpos(char *haystack, char *needle)
{
	char *p = strstr(haystack, needle);
	if (p)
	  return p - haystack;
	return -1;   // Not found = -1.
}


void decodevalue(const char *s)
{
	char *s_ptr;
	char *decoded_ptr;
	char hex_str[4] = "\0\0\0";
	unsigned int hex_val;

	s_ptr = (char *) s;
	decoded_ptr = (char *) s;

	while (*s_ptr != '\0')
	{
		if (*s_ptr == '+')
		{
			*decoded_ptr = ' ';
		}
		else if (*s_ptr == '%')
		{
			hex_str[0] = *(++s_ptr);
			hex_str[1] = *(++s_ptr);
			sscanf(hex_str, "%x", &hex_val);
			*decoded_ptr = (char) hex_val;
		}
		else
		{
			*decoded_ptr = *s_ptr;
		}
			
		s_ptr++;
		decoded_ptr++;
	}

	*decoded_ptr = '\0';
}

char *nexttoken(char *s, char separator)
{
	static char *p;
	static int at_the_end = 0;
	char *start_position;

	if (s == NULL)    /* not the first call for this string */
	{
		if (at_the_end)
			return NULL;
	}
	else              /* is the first call for this string */
	{
		p = s;
		at_the_end = 0;
	}

	start_position = p;

	while (*p)
	{
		if (*p == separator)
		{
			*p = '\0';
			p++;
			return start_position;

		}
		p++;
	}
	at_the_end = 1;
	return start_position;
}



int parse_http_uri(struct request *req, char *form_data)
{
	char *name, *value;
	
	if ((name = nexttoken(form_data, '=')) == NULL)
		return 0;
	if ((value = nexttoken(NULL, '&')) == NULL)
		return 0;
	decodevalue(value);
	
	lua_pushstring(req->vm, value);	
	lua_setfield(req->vm, -2, name); 

	while (1)
	{
		if ((name = nexttoken(NULL, '=')) == NULL)
			break;
		if ((value = nexttoken(NULL, '&')) == NULL)
			break;
		decodevalue(value);
		
		lua_pushstring(req->vm, value);	
		lua_setfield(req->vm, -2, name); 
	}

	return 1;
}


int parse_http_protocol(struct request *req, char *req_ptr, size_t req_len) 
{
	int  len, pos;
	char *line_start, *line_end, *ptr, *req_end, *uri;
	
	ptr = req_ptr;
	req_end = req_ptr + req_len;
	
	for (line_end = ptr;ptr < req_end;ptr++) {
		if (ptr >= line_end) {
			//At the end of a line, find if we have another
			line_end = ptr;
			
			//Find start of next line
			while (ptr < req_end && (*ptr == '\n' || *ptr == '\r')) {
				//If null line found return 1
				if (*ptr == '\n' && (*(ptr - 1) == '\n' || *(ptr - 2) == '\n'))
					return 1;
				ptr++;
			}
			
			line_start = ptr;
			
			//Find end of line
			while (ptr < req_end && *ptr != '\n' && *ptr != '\r') ptr++;
			
			line_end = ptr - 1;
			ptr = line_start;
		}
		
		switch (*ptr) 
		{
			case ':':
				ptr += 2;
				switch (*line_start) 
				{
					case 'H':
						if ((line_start + 3) < req_end && !strncmp(line_start+1, "ost", 3)) 
						{
				/*			len = line_end - ptr;
							conn->req.host = malloc(len + 1);
							memcpy(conn->req.host, ptr, len);
							conn->req.host[len] = '\0';
				*/		}
						break;
					case 'C':
						if ((line_start + 9) < req_end && !strncmp(line_start+1, "onnection", 9)) 
						{
					/*		if ((ptr + 10) < req_end && !strncmp(ptr, "keep-alive", 10))
								conn->req.keep_alive = 1;
							else if ((ptr + 5) < req_end && !strncmp(ptr, "close", 5))
								conn->req.keep_alive = 0;
					*/	}
				}
				ptr = line_end;
				break;
			case ' ':
				switch (*line_start) 
				{
					case 'G':
						if ((line_start + 3) < req_end && !strncmp(line_start+1, "ET", 2))
							req->http_method = HTTP_METHOD_GET;
						break;
					case 'P':
						if ((line_start + 4) < req_end && !strncmp(line_start+1, "OST", 3))
							req->http_method = HTTP_METHOD_POST;
						break;
					case 'H':
						if ((line_start + 4) < req_end && !strncmp(line_start+1, "EAD", 3))
							req->http_method = HTTP_METHOD_HEAD;
				}
				
				line_start = ptr;
				do ptr++; while (ptr < line_end && *ptr != ' ');
				
				
				if (*ptr == ' ' && (ptr + 8) == line_end && !strncmp(ptr+1, "HTTP/1.", 7)) 
				{
					/*
					if (*(ptr+8) == '0')
						conn->req.http_version = HTTP_VERSION_1_0;
					else if (*(ptr+8) == '1')
						conn->req.http_version = HTTP_VERSION_1_1;
					*/
					len  = ptr - line_start;
					len -=1;
					uri  = zmalloc(len);
					memcpy(uri, line_start+2, len-1);
					uri[len-1] = '\0';
					
					pos = strpos(uri, "?");
					if(pos > 0)
					{
						//解析GET
						lua_newtable(req->vm);
						parse_http_uri(req, uri+pos+1);
						lua_setglobal(req->vm, "_GET");
						
						uri[pos] = '\0';
					}
					
					len       = strlen(srv.root)+strlen(uri);
					req->path = zmalloc(len+1);
					strcpy(req->path, srv.root);
					strcat(req->path, uri);
					
					zfree(uri);
					uri = NULL;
				}
				
				ptr = line_end;
				break;
		}
	}
	
	return 0;
}

void processInputBuffer(struct request *req)
{
	int  pos, i;
	const char *str;

	
	if(strpos(req->buf, "\r\n\r\n") == -1)
	{
		request_change(req->fd, req, sockio_read, FDEVENT_WRITE);
	}
	
	
	parse_http_protocol(req, req->buf, sdslen(req->buf));
	
	lua_settop(srv.L, 0);
	req->vm           = lua_newthread(srv.L);
	req->ref          = luaL_ref(srv.L, LUA_REGISTRYINDEX);
	lua_pushthread(req->vm);  
	lua_pushinteger(req->vm, req->fd); 
	lua_setglobal(req->vm, "__USERDATA__");
	
	if (luaL_loadfile(req->vm, req->path)) 
	{
		str = lua_tostring(req->vm, -1);
		lua_pop(req->vm, 1);
		
		sdsclear(req->buf);
		req->buf      = sdscatprintf(req->buf, HTTP_HEADER, strlen(str),  str);
		
		request_change(req->fd, req, sockio_write, FDEVENT_WRITE);
		return ;
	}
	
	//srv.el->delete(req->fd, req);
	sdsclear(req->buf);
	lua_resume(req->vm, 0);
	
	return ;
}

void sockio_write(int fd, struct request *req)
{
	int nwritten = 0, totwritten = 0;
	
	if(req->buf == NULL)
	{
		request_delete(fd, req);
		return ;
	}
	
	totwritten = sdslen(req->buf);
	nwritten   = write(fd, req->buf+req->sentlen, totwritten-req->sentlen);
	
	DEBUG_PRINT("nwritten = %d\ttotwritten = %d\tbuf=%s\r\n", nwritten, totwritten, req->buf);
	
	if(nwritten == 0)
	{
		//发送完毕
		request_delete(fd, req);
		return ;
	}
	else if(nwritten < 0)
	{
		if(errno == EAGAIN || errno == EWOULDBLOCK)
		{
			request_change(fd, req, sockio_write, FDEVENT_WRITE);
			return ;
		}
		
		//异常退出
		//redisLog(REDIS_VERBOSE, "Writeing from client: %s", strerror(errno));
		request_delete(req->fd, req);
		return ;
	}
	
	req->sentlen += nwritten;
	
	if(req->sentlen < totwritten)
	{
		//没发送完接着再发
		request_change(fd, req, sockio_write, FDEVENT_WRITE);
		return ;
	}
	
	if(req->state == HTTP_SEND)
		request_delete(req->fd, req);
	else if(req->state == REDIS_SEND)
	{
		req->sentlen  = 0;
		req->state    = REDIS_RECV;
		sdsclear(req->buf); //清空字符串
		
		request_change(fd, req, sockio_read, FDEVENT_READ);
	}
}

void sockio_read(int fd, struct request *req)
{
	sds    tmp;
	int    nread;
	size_t len;
	char   buf[REDIS_IOBUF_LEN], *line;
	
	memset(buf, 0, REDIS_IOBUF_LEN);
	
	nread = read(fd, buf, REDIS_IOBUF_LEN);
	DEBUG_PRINT("fd = %d\tnread = %d\tbuf=%s\r\n", fd, nread, buf);
	
	if (nread == -1) 
	{
		if(errno == EAGAIN || errno == EWOULDBLOCK)
		{
			//重新读取
			request_change(fd, req, sockio_read, FDEVENT_READ);
			return ;
		}
		else 
		{
			//printf("Reading from client: %s", strerror(errno));
			request_delete(req->fd, req);
			return;
		}
	} 
	else if (nread == 0) 
	{
		//puts("Client closed connection");
		request_delete(req->fd, req);
		return;
	}
	
	req->buf = sdscatlen(req->buf, buf, nread);
	
	if(req->state == HTTP_RECV)
	{
		///处理HTTP协议的
		processInputBuffer(req);
		return ;
	}
	else if(req->state == REDIS_RECV)
	{
		parse_redis_protocol(req);
		linkpool_free(req->link);
		req->state = HTTP_SEND;
		req->link  = NULL;
		lua_resume(req->vm, 1); 
		return ;
	}
	
	request_change(fd, req, sockio_write, FDEVENT_WRITE);
}

void *acceptTcpHandler()
{
	int cport, cfd;
	char cip[128];
	
	while(!srv.quit)
	{
		cfd = anetTcpAccept(srv.neterr, srv.listenfd, cip, &cport);
		if (cfd == ANET_ERR) 
		{
			//printf("Accepting client connection: %s", srv.neterr);
			continue;
		}
		request_new(cfd, sockio_read, cip);
	}
}


bool parse_conf()
{
	int  ind, port;
	struct pool_t  *pool;
	const char     *key, *value;
	lua_State      *L; 
	
	backend = hash_new(10);
	L       = luaL_newstate();
	luaL_openlibs(L);

	if (luaL_loadfile(L, "./config.lua") || lua_pcall(L, 0, 0, 0)) 
	{
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
		lua_pop(L, 1); /* remove the error-msg */
		exit(EXIT_FAILURE); 
	}
	
	lua_getglobal(L, "port"); 
	srv.port  = lua_tointeger(L, -1);   
	lua_pop(L, 2);  
	
	
	lua_getglobal(L, "root"); 
	srv.root  = zstrdup(lua_tostring(L, -1));   
	lua_pop(L, 2);  

	lua_getglobal(L, "timeout"); 
	srv.timeout  = lua_tointeger(L, -1);   
	lua_pop(L, 2);  

	lua_getglobal(L, "connections"); 
	srv.events   = lua_tointeger(L, -1);   
	lua_pop(L, 2);  
	
	
	lua_getglobal(L, "pool"); 
	if (!lua_istable(L, -1))
	{
		fprintf(stderr, "pool: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1); // remove the error-msg 
		exit(EXIT_FAILURE); 
	}
	
	ind = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0)
	{
		key   = lua_tostring(L, -2);
		
		if(strcmp("min", key) == 0)
			srv.pool_min = lua_tointeger(L, -1);   
		else if(strcmp("max", key) == 0)
			srv.pool_max = lua_tointeger(L, -1);  
		
		lua_pop( L, 1 );
	}
	
	//DEBUG_PRINT("pool min = %d\tmax = %d\r\n", srv.pool_min, srv.pool_max);
	
	lua_getglobal(L, "host"); 
		
	if (!lua_istable(L, -1))
	{
		fprintf(stderr, "%s\n", lua_tostring(L, -1));
		lua_pop(L, 1); // remove the error-msg 
		return false; 
	}
	ind = lua_gettop(L);
	lua_pushnil(L);	
	while (lua_next(L, -2) != 0)
	{
		if (lua_istable(L, -1))
		{
			ind = 1;
			lua_pushnil(L);
			
			pool = zmalloc(sizeof(struct pool_t));
			while (lua_next(L, -2) != 0)
			{
				key   = lua_tostring(L, -2);
				
				if(strcmp("id", key) == 0)
				{
					pool->id = zstrdup(lua_tostring(L, -1));
				}
				else if(strcmp("port", key) == 0)
				{
					port = lua_tointeger(L, -1);
				}
				else if(strcmp("host", key) == 0)
				{
					value = lua_tostring(L, -1);
				}
				
				lua_pop( L, 1 );
			}
			
			compose_addr(&pool->addr, value, port);
			//DEBUG_PRINT("%s=>%s:%d\n", pool->id, pool->host, pool->port);
			hash_add(backend, pool->id, (void *)pool);
		}
		lua_pop(L, 1); 
	}
	lua_close(L);

	return true;
}

/*
void redisLog(int level, const char *fmt, ...) 
{
	const char *c = ".-*#";
	time_t now = time(NULL);
	va_list ap;
	FILE *fp;
	char buf[64];
	char msg[REDIS_MAX_LOGMSG_LEN];


	fp = (server.logfile == NULL) ? stdout : fopen(server.logfile,"a");
	if (!fp) return;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	strftime(buf,sizeof(buf),"%d %b %H:%M:%S",localtime(&now));
	fprintf(fp,"[%d] %s %c %s\n",(int)getpid(), buf, c[level], msg);
	fflush(fp);

	if (server.logfile) fclose(fp);
}
*/

void pool_initialize(const char *key, void *value)
{
	struct pool_t  *pool = (struct pool_t *)value;
	
	pool_init(pool);
}


int sockio_echo(lua_State *vm)
{
	int sockfd;
	struct request *req;
	const char     *str = luaL_checkstring(vm, 1);
	
	lua_getglobal(vm, "__USERDATA__"); 
	sockfd  = luaL_checkinteger(vm, -1);   
	lua_pop(vm, 1);  
	
	req = &srv.req[sockfd];
	DEBUG_PRINT("str = %s\r\n", str);
	
	sdsclear(req->buf);
	req->state    = HTTP_SEND;
	req->buf      = sdscatprintf(req->buf, HTTP_HEADER, strlen(str),  str);
	
	request_change(req->fd, req, sockio_write, FDEVENT_WRITE);
	return lua_yield(vm, 0);
}

int main(int argc, char **argv) 
{
	
	pthread_t      tid;
	pthread_attr_t attr;
	
	sig_handle(SIGPIPE, SIG_IGN);
	parse_conf();
	
	
	srv.L = luaL_newstate();
	luaL_openlibs(srv.L);
	
	static const struct luaL_reg sockio_lib[] = {
		{"echo", sockio_echo},
		{NULL,   NULL}
	};
	
	luaL_openlib(srv.L, "redis",  redis_lib,  0);
	luaL_openlib(srv.L, "sockio", sockio_lib, 0);
	
	LL_INIT(&srv.timer);
	srv.logfile   = zstrdup("./redis.log");
	srv.bindaddr  = NULL;
	srv.quit      = false;
	srv.req       = zcalloc(srv.events, sizeof(struct request));
	srv.el        = fdevent_init(srv.events);
	srv.listenfd  = anetTcpServer(srv.neterr, srv.port, srv.bindaddr);
	if (srv.listenfd == ANET_ERR) 
	{
		//redisLog(REDIS_WARNING, "Opening port: %s", server.neterr);
		exit(1);
	}
	
	printf("hash_size = %d\r\n", hash_size(backend));
	hash_foreach(backend, pool_initialize);
	if (pthread_attr_init(&attr) != 0)
		err("pthread_attr_init");

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
		err("pthread_attr_setdetachstate");
	
	if (pthread_attr_setstacksize(&attr, 20 << 20) != 0)
		err("pthread_attr_setstacksize");
	
	pthread_create(&tid, &attr, acceptTcpHandler, NULL);
	
	srv.el->watch();
	return 0;
}
