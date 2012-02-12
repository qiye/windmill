#ifndef __SERVER_H__
#define __SERVER_H__

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/uio.h>
#include <inttypes.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/resource.h>


typedef enum  
{
	LINK_POOL,
	HTTP_RECV, 
	HTTP_SEND, 
	REDIS_SEND, 
	REDIS_RECV,
	MEMCACHED_SEND,
	MEMCACHED_RECV,
	TOKYOTYRANT_SEND,
	TOKYOTYRANT_RECV
}HTTP_STATE;


#include "sds.h"
#include "anet.h"
#include "llist.h"
#include "zmalloc.h"
#include "fdevent.h"
#include "request.h"
#include "timer.h"
#include "hash.h"
#include "pool.h"
#include "module/redis.h"

#define REQUEST_TIMEOUT        60
#define REDBRIDGE_VERSION      "RedBridge/1.0"
#define REDIS_IOBUF_LEN        4092
#define HTTP_HEADER            "HTTP/1.1 200 OK\r\n" REDBRIDGE_VERSION "\r\nContent-Type: text/html\r\nContent-Length: %d\r\nCache-Control: no-cache, must-revalidate\r\nPragma: no-cache\r\nExpires: -1\r\n\r\n%s"

#ifdef DEBUG
	#define DEBUG_PRINT(fmt, args...) printf("%s\t%s\t%d\t "fmt, __FILE__, __FUNCTION__, __LINE__, args)                                                                          
#else
	#define DEBUG_PRINT(fmt, args...)
#endif




struct server
{
	bool            quit;
	ushort          port;
	int             listenfd;
	
	struct request *req;
	uint16_t        timeout;
	uint32_t        events;
	struct fdevent  *el;
	struct llhead   timer;       //¶¨Ê±Æ÷
	
	lua_State      *L; 
	char           *root;    //lua script dir
	
	uint16_t       pool_min;
	uint16_t       pool_max;
	
	char *logfile;
	char *bindaddr;
	char neterr[ANET_ERR_LEN];
};

struct server srv;


hash *backend;

int strpos(char *haystack, char *needle);

void sockio_write(int fd, struct request *req);


#endif
