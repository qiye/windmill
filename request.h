#ifndef __LOONG_SSO_REQUEST_H__
#define __LOONG_SSO_REQUEST_H__


struct request;

typedef void PF(int fd, struct request *req);


typedef enum {
	HTTP_METHOD_GET,
	HTTP_METHOD_POST,
	HTTP_METHOD_HEAD,
	HTTP_METHOD_UNKNOWN
} http_method_t;


struct request
{
	int          fd;
	sds          buf;
	time_t       now;
	size_t       sentlen;
	HTTP_STATE   state;
	
	char         *pool_id;    //pool key name
	char         *path;
	char         ip[16];
	lua_State    *vm;
	int          ref;

	http_method_t   http_method;
	PF              *handler;
	struct request  *link;     //连接池的Redis链接
	struct llhead   entry;
};


bool request_initialize(int fd, PF *handler);

void request_delete(int fd, struct request *req);

bool request_new(int fd, PF *handler, char *ip);

void request_change(int fd, struct request *req, PF *handler, fdevent_t filter);

#endif
