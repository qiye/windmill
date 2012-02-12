#ifndef __REDIS_H__
#define __REDIS_H__


#define REDIS_ERR -1
#define REDIS_OK 0

#define REDIS_BLOCK 0x1
#define REDIS_REPLY_STRING 1
#define REDIS_REPLY_ARRAY 2
#define REDIS_REPLY_INTEGER 3
#define REDIS_REPLY_NIL 4
#define REDIS_REPLY_STATUS 5
#define REDIS_REPLY_ERROR 6



#define REDIS_ERR_IO 1 /* Error in read or write */
#define REDIS_ERR_EOF 3 /* End of file */
#define REDIS_ERR_PROTOCOL 4 /* Protocol error */
#define REDIS_ERR_OOM 5 /* Out of memory */
#define REDIS_ERR_OTHER 2 /* Everything else... */


/* This is the reply object returned by redisCommand() */
typedef struct redisReply {
	int type; /* REDIS_REPLY_* */
	long long integer; /* The integer when type is REDIS_REPLY_INTEGER */
	int len; /* Length of string */
	char *str; /* Used for both REDIS_REPLY_ERROR and REDIS_REPLY_STRING */
	size_t elements; /* number of elements, for REDIS_REPLY_ARRAY */
	struct redisReply **element; /* elements vector for REDIS_REPLY_ARRAY */
} redisReply;

typedef struct redisReadTask {
	int type;
	int elements; /* number of elements in multibulk container */
	int idx; /* index in parent (array) object */
	void *obj; /* holds user-generated value for a read task */
	struct redisReadTask *parent; /* parent task */
	void *privdata; /* user-settable arbitrary field */
} redisReadTask;

typedef struct redisReplyObjectFunctions {
	void *(*createString)(const redisReadTask*, char*, size_t);
	void *(*createArray)(const redisReadTask*, int);
	void *(*createInteger)(const redisReadTask*, long long);
	void *(*createNil)(const redisReadTask*);
	void (*freeObject)(void*);
} redisReplyObjectFunctions;

/* State for the protocol parser */
typedef struct redisReader {
	int err; /* Error flags, 0 when there is no error */
	char errstr[128]; /* String representation of error when applicable */

	char *buf; /* Read buffer */
	size_t pos; /* Buffer cursor */
	size_t len; /* Buffer length */

	redisReadTask rstack[3];
	int ridx; /* Index of current read task */
	void *reply; /* Temporary reply pointer */

	redisReplyObjectFunctions *fn;
	void *privdata;
} redisReader;

int redis_del(lua_State *vm);

int redis_set(lua_State *vm);

int redis_get(lua_State *vm);

int redis_incr(lua_State *vm);

int redis_decr(lua_State *vm);

int redis_llen(lua_State *vm);

int redis_lpop(lua_State *vm);



int redis_select(lua_State *vm);

int redis_lindex(lua_State *vm);

int redis_lpush(lua_State *vm);

int redis_rpush(lua_State *vm);

bool parse_redis_protocol(struct request *req);


static const struct luaL_reg redis_lib[] = {
	{"del",     redis_del},
	{"set",     redis_set},
	{"get",     redis_get},
	{"incr",    redis_incr},
	{"llen",    redis_llen},
	{"lpop",    redis_lpop},
	{"decr",    redis_decr},
	{"rpush",   redis_rpush},
	{"lpush",   redis_lpush},
	{"lindex",  redis_lindex},
	{"select",  redis_select},
	{NULL,    NULL}
};

#endif
