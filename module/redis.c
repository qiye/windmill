#include "server.h"

#define LUAHIREDIS_CONST_MT "lua-redis.const"
#define LUAHIREDIS_KEY_NIL  "NIL"


void freeReplyObject(void *reply);

static redisReply *createReplyObject(int type);
static void *createStringObject(const redisReadTask *task, char *str, size_t len);
static void *createArrayObject(const redisReadTask *task, int elements);
static void *createIntegerObject(const redisReadTask *task, long long value);
static void *createNilObject(const redisReadTask *task);

/* Default set of functions to build the reply. Keep in mind that such a
 * function returning NULL is interpreted as OOM. */
static redisReplyObjectFunctions defaultFunctions = {
	createStringObject,
	createArrayObject,
	createIntegerObject,
	createNilObject,
	freeReplyObject
};


static int lconst_tostring(lua_State * L)
{
  /*
  * Assuming we have correct argument type.
  * Should be reasonably safe, since this is a metamethod.
  */
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, 1, "name"); /* TODO: Do we need fancier representation? */

  return 1;
}

/* const API */
static const struct luaL_reg CONST_MT[] =
{
  { "__tostring", lconst_tostring },

  { NULL, NULL }
};



/*
void __redisSetError(redisContext *c, int type, const char *str) {
	size_t len;

	c->err = type;
	if (str != NULL) {
		len = strlen(str);
		len = len < (sizeof(c->errstr)-1) ? len : (sizeof(c->errstr)-1);
		memcpy(c->errstr,str,len);
		c->errstr[len] = '\0';
	} else {
		// Only REDIS_ERR_IO may lack a description! 
		assert(type == REDIS_ERR_IO);
		strerror_r(errno,c->errstr,sizeof(c->errstr));
	}
}
*/

/* Create a reply object */
static redisReply *createReplyObject(int type) {
	redisReply *r = calloc(1,sizeof(*r));

	if (r == NULL)
		return NULL;

	r->type = type;
	return r;
}

/* Free a reply object */
void freeReplyObject(void *reply) {
	redisReply *r = reply;
	size_t j;

	switch(r->type) {
	case REDIS_REPLY_INTEGER:
		break; /* Nothing to free */
	case REDIS_REPLY_ARRAY:
		if (r->element != NULL) {
			for (j = 0; j < r->elements; j++)
				if (r->element[j] != NULL)
					freeReplyObject(r->element[j]);
			free(r->element);
		}
		break;
	case REDIS_REPLY_ERROR:
	case REDIS_REPLY_STATUS:
	case REDIS_REPLY_STRING:
		if (r->str != NULL)
			free(r->str);
		break;
	}
	free(r);
}

static void *createStringObject(const redisReadTask *task, char *str, size_t len) {
	redisReply *r, *parent;
	char *buf;

	r = createReplyObject(task->type);
	if (r == NULL)
		return NULL;

	buf = malloc(len+1);
	if (buf == NULL) {
		freeReplyObject(r);
		return NULL;
	}

	assert(task->type == REDIS_REPLY_ERROR  ||
		   task->type == REDIS_REPLY_STATUS ||
		   task->type == REDIS_REPLY_STRING);

	/* Copy string value */
	memcpy(buf,str,len);
	buf[len] = '\0';
	r->str = buf;
	r->len = len;

	if (task->parent) {
		parent = task->parent->obj;
		assert(parent->type == REDIS_REPLY_ARRAY);
		parent->element[task->idx] = r;
	}
	return r;
}

static void *createArrayObject(const redisReadTask *task, int elements) {
	redisReply *r, *parent;

	r = createReplyObject(REDIS_REPLY_ARRAY);
	if (r == NULL)
		return NULL;

	if (elements > 0) {
		r->element = calloc(elements,sizeof(redisReply*));
		if (r->element == NULL) {
			freeReplyObject(r);
			return NULL;
		}
	}

	r->elements = elements;

	if (task->parent) {
		parent = task->parent->obj;
		assert(parent->type == REDIS_REPLY_ARRAY);
		parent->element[task->idx] = r;
	}
	return r;
}

static void *createIntegerObject(const redisReadTask *task, long long value) {
	redisReply *r, *parent;

	r = createReplyObject(REDIS_REPLY_INTEGER);
	if (r == NULL)
		return NULL;

	r->integer = value;

	if (task->parent) {
		parent = task->parent->obj;
		assert(parent->type == REDIS_REPLY_ARRAY);
		parent->element[task->idx] = r;
	}
	return r;
}

static void *createNilObject(const redisReadTask *task) {
	redisReply *r, *parent;

	r = createReplyObject(REDIS_REPLY_NIL);
	if (r == NULL)
		return NULL;

	if (task->parent) {
		parent = task->parent->obj;
		assert(parent->type == REDIS_REPLY_ARRAY);
		parent->element[task->idx] = r;
	}
	return r;
}

static void __redisReaderSetError(redisReader *r, int type, const char *str) {
	size_t len;

	if (r->reply != NULL && r->fn && r->fn->freeObject) {
		r->fn->freeObject(r->reply);
		r->reply = NULL;
	}

	/* Clear input buffer on errors. */
	if (r->buf != NULL) {
		sdsfree(r->buf);
		r->buf = NULL;
		r->pos = r->len = 0;
	}

	/* Reset task stack. */
	r->ridx = -1;

	/* Set error. */
	r->err = type;
	len = strlen(str);
	len = len < (sizeof(r->errstr)-1) ? len : (sizeof(r->errstr)-1);
	memcpy(r->errstr,str,len);
	r->errstr[len] = '\0';
}

static size_t chrtos(char *buf, size_t size, char byte) {
	size_t len = 0;

	switch(byte) {
	case '\\':
	case '"':
		len = snprintf(buf,size,"\"\\%c\"",byte);
		break;
	case '\n': len = snprintf(buf,size,"\"\\n\""); break;
	case '\r': len = snprintf(buf,size,"\"\\r\""); break;
	case '\t': len = snprintf(buf,size,"\"\\t\""); break;
	case '\a': len = snprintf(buf,size,"\"\\a\""); break;
	case '\b': len = snprintf(buf,size,"\"\\b\""); break;
	default:
		if (isprint(byte))
			len = snprintf(buf,size,"\"%c\"",byte);
		else
			len = snprintf(buf,size,"\"\\x%02x\"",(unsigned char)byte);
		break;
	}

	return len;
}

static void __redisReaderSetErrorProtocolByte(redisReader *r, char byte) {
	char cbuf[8], sbuf[128];

	chrtos(cbuf,sizeof(cbuf),byte);
	snprintf(sbuf,sizeof(sbuf),
		"Protocol error, got %s as reply type byte", cbuf);
	__redisReaderSetError(r,REDIS_ERR_PROTOCOL,sbuf);
}

static void __redisReaderSetErrorOOM(redisReader *r) {
	__redisReaderSetError(r,REDIS_ERR_OOM,"Out of memory");
}

static char *readBytes(redisReader *r, unsigned int bytes) {
	char *p;
	if (r->len-r->pos >= bytes) {
		p = r->buf+r->pos;
		r->pos += bytes;
		return p;
	}
	return NULL;
}

/* Find pointer to \r\n. */
static char *seekNewline(char *s, size_t len) {
	int pos = 0;
	int _len = len-1;

	/* Position should be < len-1 because the character at "pos" should be
	 * followed by a \n. Note that strchr cannot be used because it doesn't
	 * allow to search a limited length and the buffer that is being searched
	 * might not have a trailing NULL character. */
	while (pos < _len) {
		while(pos < _len && s[pos] != '\r') pos++;
		if (s[pos] != '\r') {
			/* Not found. */
			return NULL;
		} else {
			if (s[pos+1] == '\n') {
				/* Found. */
				return s+pos;
			} else {
				/* Continue searching. */
				pos++;
			}
		}
	}
	return NULL;
}

/* Read a long long value starting at *s, under the assumption that it will be
 * terminated by \r\n. Ambiguously returns -1 for unexpected input. */
static long long readLongLong(char *s) {
	long long v = 0;
	int dec, mult = 1;
	char c;

	if (*s == '-') {
		mult = -1;
		s++;
	} else if (*s == '+') {
		mult = 1;
		s++;
	}

	while ((c = *(s++)) != '\r') {
		dec = c - '0';
		if (dec >= 0 && dec < 10) {
			v *= 10;
			v += dec;
		} else {
			/* Should not happen... */
			return -1;
		}
	}

	return mult*v;
}

static char *readLine(redisReader *r, int *_len) {
	char *p, *s;
	int len;

	p = r->buf+r->pos;
	s = seekNewline(p,(r->len-r->pos));
	if (s != NULL) {
		len = s-(r->buf+r->pos);
		r->pos += len+2; /* skip \r\n */
		if (_len) *_len = len;
		return p;
	}
	return NULL;
}

static void moveToNextTask(redisReader *r) {
	redisReadTask *cur, *prv;
	while (r->ridx >= 0) {
		/* Return a.s.a.p. when the stack is now empty. */
		if (r->ridx == 0) {
			r->ridx--;
			return;
		}

		cur = &(r->rstack[r->ridx]);
		prv = &(r->rstack[r->ridx-1]);
		assert(prv->type == REDIS_REPLY_ARRAY);
		if (cur->idx == prv->elements-1) {
			r->ridx--;
		} else {
			/* Reset the type because the next item can be anything */
			assert(cur->idx < prv->elements);
			cur->type = -1;
			cur->elements = -1;
			cur->idx++;
			return;
		}
	}
}

static int processLineItem(redisReader *r) {
	redisReadTask *cur = &(r->rstack[r->ridx]);
	void *obj;
	char *p;
	int len;

	if ((p = readLine(r,&len)) != NULL) {
		if (cur->type == REDIS_REPLY_INTEGER) {
			if (r->fn && r->fn->createInteger)
				obj = r->fn->createInteger(cur,readLongLong(p));
			else
				obj = (void*)REDIS_REPLY_INTEGER;
		} else {
			/* Type will be error or status. */
			if (r->fn && r->fn->createString)
				obj = r->fn->createString(cur,p,len);
			else
				obj = (void*)(size_t)(cur->type);
		}

		if (obj == NULL) {
			__redisReaderSetErrorOOM(r);
			return REDIS_ERR;
		}

		/* Set reply if this is the root object. */
		if (r->ridx == 0) r->reply = obj;
		moveToNextTask(r);
		return REDIS_OK;
	}

	return REDIS_ERR;
}

static int processBulkItem(redisReader *r) {
	redisReadTask *cur = &(r->rstack[r->ridx]);
	void *obj = NULL;
	char *p, *s;
	long len;
	unsigned long bytelen;
	int success = 0;

	p = r->buf+r->pos;
	s = seekNewline(p,r->len-r->pos);
	if (s != NULL) {
		p = r->buf+r->pos;
		bytelen = s-(r->buf+r->pos)+2; /* include \r\n */
		len = readLongLong(p);

		if (len < 0) {
			/* The nil object can always be created. */
			if (r->fn && r->fn->createNil)
				obj = r->fn->createNil(cur);
			else
				obj = (void*)REDIS_REPLY_NIL;
			success = 1;
		} else {
			/* Only continue when the buffer contains the entire bulk item. */
			bytelen += len+2; /* include \r\n */
			if (r->pos+bytelen <= r->len) {
				if (r->fn && r->fn->createString)
					obj = r->fn->createString(cur,s+2,len);
				else
					obj = (void*)REDIS_REPLY_STRING;
				success = 1;
			}
		}

		/* Proceed when obj was created. */
		if (success) {
			if (obj == NULL) {
				__redisReaderSetErrorOOM(r);
				return REDIS_ERR;
			}

			r->pos += bytelen;

			/* Set reply if this is the root object. */
			if (r->ridx == 0) r->reply = obj;
			moveToNextTask(r);
			return REDIS_OK;
		}
	}

	return REDIS_ERR;
}

static int processMultiBulkItem(redisReader *r) {
	redisReadTask *cur = &(r->rstack[r->ridx]);
	void *obj;
	char *p;
	long elements;
	int root = 0;

	/* Set error for nested multi bulks with depth > 1 */
	if (r->ridx == 2) {
		__redisReaderSetError(r,REDIS_ERR_PROTOCOL,
			"No support for nested multi bulk replies with depth > 1");
		return REDIS_ERR;
	}

	if ((p = readLine(r,NULL)) != NULL) {
		elements = readLongLong(p);
		root = (r->ridx == 0);

		if (elements == -1) {
			if (r->fn && r->fn->createNil)
				obj = r->fn->createNil(cur);
			else
				obj = (void*)REDIS_REPLY_NIL;

			if (obj == NULL) {
				__redisReaderSetErrorOOM(r);
				return REDIS_ERR;
			}

			moveToNextTask(r);
		} else {
			if (r->fn && r->fn->createArray)
				obj = r->fn->createArray(cur,elements);
			else
				obj = (void*)REDIS_REPLY_ARRAY;

			if (obj == NULL) {
				__redisReaderSetErrorOOM(r);
				return REDIS_ERR;
			}

			/* Modify task stack when there are more than 0 elements. */
			if (elements > 0) {
				cur->elements = elements;
				cur->obj = obj;
				r->ridx++;
				r->rstack[r->ridx].type = -1;
				r->rstack[r->ridx].elements = -1;
				r->rstack[r->ridx].idx = 0;
				r->rstack[r->ridx].obj = NULL;
				r->rstack[r->ridx].parent = cur;
				r->rstack[r->ridx].privdata = r->privdata;
			} else {
				moveToNextTask(r);
			}
		}

		/* Set reply if this is the root object. */
		if (root) r->reply = obj;
		return REDIS_OK;
	}

	return REDIS_ERR;
}

static int processItem(redisReader *r) {
	redisReadTask *cur = &(r->rstack[r->ridx]);
	char *p;

	/* check if we need to read type */
	if (cur->type < 0) {
		if ((p = readBytes(r,1)) != NULL) {
			switch (p[0]) {
			case '-':
				cur->type = REDIS_REPLY_ERROR;
				break;
			case '+':
				cur->type = REDIS_REPLY_STATUS;
				break;
			case ':':
				cur->type = REDIS_REPLY_INTEGER;
				break;
			case '$':
				cur->type = REDIS_REPLY_STRING;
				break;
			case '*':
				cur->type = REDIS_REPLY_ARRAY;
				break;
			default:
				__redisReaderSetErrorProtocolByte(r,*p);
				return REDIS_ERR;
			}
		} else {
			/* could not consume 1 byte */
			return REDIS_ERR;
		}
	}

	/* process typed item */
	switch(cur->type) {
	case REDIS_REPLY_ERROR:
	case REDIS_REPLY_STATUS:
	case REDIS_REPLY_INTEGER:
		return processLineItem(r);
	case REDIS_REPLY_STRING:
		return processBulkItem(r);
	case REDIS_REPLY_ARRAY:
		return processMultiBulkItem(r);
	default:
		assert(NULL);
		return REDIS_ERR; /* Avoid warning. */
	}
}

redisReader *redisReaderCreate(void) {
	redisReader *r;

	r = calloc(sizeof(redisReader),1);
	if (r == NULL)
		return NULL;

	r->err = 0;
	r->errstr[0] = '\0';
	r->fn = &defaultFunctions;
	r->buf = sdsempty();
	if (r->buf == NULL) {
		free(r);
		return NULL;
	}

	r->ridx = -1;
	return r;
}

void redisReaderFree(redisReader *r) {
	if (r->reply != NULL && r->fn && r->fn->freeObject)
		r->fn->freeObject(r->reply);
	if (r->buf != NULL)
		sdsfree(r->buf);
	free(r);
}

int redisReaderFeed(redisReader *r,  char *buf, size_t len) {
	sds newbuf;

	/* Return early when this reader is in an erroneous state. */
	if (r->err)
		return REDIS_ERR;

	/* Copy the provided buffer. */
	if (buf != NULL && len >= 1) {
		/* Destroy internal buffer when it is empty and is quite large. */
		if (r->len == 0 && sdsavail(r->buf) > 16*1024) {
			sdsfree(r->buf);
			r->buf = sdsempty();
			r->pos = 0;

			/* r->buf should not be NULL since we just free'd a larger one. */
			assert(r->buf != NULL);
		}

		newbuf = sdscatlen(r->buf,buf,len);
		if (newbuf == NULL) {
			__redisReaderSetErrorOOM(r);
			return REDIS_ERR;
		}

		r->buf = newbuf;
		r->len = sdslen(r->buf);
	}

	return REDIS_OK;
}

int redisReaderGetReply(redisReader *r, void **reply) {
	/* Default target pointer to NULL. */
	if (reply != NULL)
		*reply = NULL;

	/* Return early when this reader is in an erroneous state. */
	if (r->err)
		return REDIS_ERR;

	/* When the buffer is empty, there will never be a reply. */
	if (r->len == 0)
		return REDIS_OK;

	/* Set first item to process when the stack is empty. */
	if (r->ridx == -1) {
		r->rstack[0].type = -1;
		r->rstack[0].elements = -1;
		r->rstack[0].idx = -1;
		r->rstack[0].obj = NULL;
		r->rstack[0].parent = NULL;
		r->rstack[0].privdata = r->privdata;
		r->ridx = 0;
	}

	/* Process items in reply. */
	while (r->ridx >= 0)
		if (processItem(r) != REDIS_OK)
			break;

	/* Return ASAP when an error occurred. */
	if (r->err)
		return REDIS_ERR;

	/* Discard part of the buffer when we've consumed at least 1k, to avoid
	 * doing unnecessary calls to memmove() in sds.c. */
	if (r->pos >= 1024) {
		r->buf = sdsrange(r->buf,r->pos,-1);
		r->pos = 0;
		r->len = sdslen(r->buf);
	}

	/* Emit a reply when there is one. */
	if (r->ridx == -1) {
		if (reply != NULL)
			*reply = r->reply;
		r->reply = NULL;
	}
	return REDIS_OK;
}

//----·Ö¸ô·û----


static int intlen(int i) 
{
	int len = 0;
	if (i < 0) {
		len++;
		i = -i;
	}
	do {
		len++;
		i /= 10;
	} while(i);
	return len;
}

/* Helper function for redisvFormatCommand(). */
static void addArgument(sds a, char ***argv, int *argc, int *totlen) 
{
	(*argc)++;
	if ((*argv = zrealloc(*argv, sizeof(char*)*(*argc))) == NULL) return ;
	if (totlen) *totlen = *totlen+1+intlen(sdslen(a))+2+sdslen(a)+2;
	(*argv)[(*argc)-1] = a;
}


int redisvFormatCommand(char **target, const char *format, va_list ap) 
{
	size_t size;
	const char *arg, *c = format;
	char *cmd = NULL; // final command 
	int pos; // position in final command 
	sds current; // current argument 
	int touched = 0; // was the current argument touched? 
	char **argv = NULL;
	int argc = 0, j;
	int totlen = 0;

	// Abort if there is not target to set 
	if (target == NULL)
		return -1;

	// Build the command string accordingly to protocol 
	current = sdsempty();
	while(*c != '\0') {
		if (*c != '%' || c[1] == '\0') {
			if (*c == ' ') {
				if (touched) {
					addArgument(current, &argv, &argc, &totlen);
					current = sdsempty();
					touched = 0;
				}
			} else {
				current = sdscatlen(current, (void *)c, 1);
				touched = 1;
			}
		} else {
			switch(c[1]) {
			case 's':
				arg = va_arg(ap,char*);
				size = strlen(arg);
				if (size > 0)
					current = sdscatlen(current, (void *)arg, size);
				break;
			case 'b':
				arg = va_arg(ap,char*);
				size = va_arg(ap,size_t);
				if (size > 0)
					current = sdscatlen(current, (void *)arg, size);
				break;
			case '%':
				current = sdscat(current,"%");
				break;
			default:
				// Try to detect printf format 
				{
					char _format[16];
					const char *_p = c+1;
					size_t _l = 0;
					va_list _cpy;

					// Flags 
					if (*_p != '\0' && *_p == '#') _p++;
					if (*_p != '\0' && *_p == '0') _p++;
					if (*_p != '\0' && *_p == '-') _p++;
					if (*_p != '\0' && *_p == ' ') _p++;
					if (*_p != '\0' && *_p == '+') _p++;

					// Field width 
					while (*_p != '\0' && isdigit(*_p)) _p++;

					// Precision 
					if (*_p == '.') {
						_p++;
						while (*_p != '\0' && isdigit(*_p)) _p++;
					}

					// Modifiers 
					if (*_p != '\0') {
						if (*_p == 'h' || *_p == 'l') {
							// Allow a single repetition for these modifiers 
							if (_p[0] == _p[1]) _p++;
							_p++;
						}
					}

					// Conversion specifier 
					if (*_p != '\0' && strchr("diouxXeEfFgGaA",*_p) != NULL) {
						_l = (_p+1)-c;
						if (_l < sizeof(_format)-2) {
							memcpy(_format,c,_l);
							_format[_l] = '\0';
							va_copy(_cpy,ap);
							current = sdscatvprintf(current,_format,_cpy);
							va_end(_cpy);

							// Update current position (note: outer blocks
							// increment c twice so compensate here) 
							c = _p-1;
						}
					}

					// Consume and discard vararg 
					va_arg(ap,void);
				}
			}
			touched = 1;
			c++;
		}
		c++;
	}

	// Add the last argument if needed 
	if (touched) {
		addArgument(current, &argv, &argc, &totlen);
	} else {
		sdsfree(current);
	}

	// Add bytes needed to hold multi bulk count 
	totlen += 1+intlen(argc)+2;

	// Build the command at protocol level 
	cmd = zmalloc(totlen+1);
	if (!cmd) return -1;
	
	pos = sprintf(cmd,"*%d\r\n",argc);
	for (j = 0; j < argc; j++) {
		pos += sprintf(cmd+pos,"$%zu\r\n",sdslen(argv[j]));
		memcpy(cmd+pos,argv[j],sdslen(argv[j]));
		pos += sdslen(argv[j]);
		sdsfree(argv[j]);
		cmd[pos++] = '\r';
		cmd[pos++] = '\n';
	}
	assert(pos == totlen);
	zfree(argv);
	cmd[totlen] = '\0';
	*target = cmd;
	return totlen;
}

static bool parse_reply_string(struct request *req)
{
	int   pos;
	ulong len;
	
	
	len  = strtoul(req->buf+1, NULL, 10);
	if(len < 1)
	{
		lua_pushboolean(req->vm, false);
		return true;
	}
	pos  = strpos(req->buf, "\r\n");
	pos += 2;
	if(sdslen(req->buf) < (len+pos))
		return false;
	
	req->buf = sdsrange(req->buf, pos, pos+len);
	lua_pushlstring(req->vm, req->buf, len);
	return true;

}


static int push_new_const(
	lua_State * L,
	const char * name,
	size_t name_len,
	int type
  )
{
  /* We trust that user would not change these values */
  lua_createtable(L, 0, 2);
  lua_pushlstring(L, name, name_len);
  lua_setfield(L, -2, "name");
  lua_pushinteger(L, type);
  lua_setfield(L, -2, "type");

  if (luaL_newmetatable(L, LUAHIREDIS_CONST_MT))
  {
	luaL_register(L, NULL, CONST_MT);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushliteral(L, LUAHIREDIS_CONST_MT);
	lua_setfield(L, -2, "__metatable");
  }

  lua_setmetatable(L, -2);

  return 1;
}


static int push_reply(lua_State * L, redisReply * pReply)
{
  /* int base = lua_gettop(L); */

  switch(pReply->type)
  {
	case REDIS_REPLY_STATUS:
	  lua_pushvalue(L, lua_upvalueindex(1)); /* M (module table) */

	  lua_pushlstring(L, pReply->str, pReply->len); /* status */
	  lua_gettable(L, -2); /* M[status] */

	  if (lua_isnil(L, -1)) /* Not bothering with metatables */
	  {
		/*
		* TODO: Following code is likely to be broken due to early binding
		* (imagine that RETURN is a command that returns given string
		* as a status):
		*
		*    assert(conn:command("RETURN", "FOO") == hiredis.FOO)
		*
		* Here hiredis.FOO would be nil before conn:command() is called.
		*
		* Note that this is not relevant to the current Redis implementation
		* (that is 2.2 and before), since it seems that it wouldn't
		* return any status code except OK, QUEUED or PONG,
		* all of which are already covered.
		*/
		lua_pushlstring(L, pReply->str, pReply->len); /* status */
		push_new_const(
			L, pReply->str, pReply->len, REDIS_REPLY_STATUS /* const */
		  );
		lua_settable(L, -3); /* M[status] = const */

		lua_pushlstring(L, pReply->str, pReply->len); /* status */
		lua_gettable(L, -2); /* return M[status] */
	  }

	  lua_remove(L, -2); /* Remove module table */

	  break;

	case REDIS_REPLY_ERROR:
	  /* Not caching errors, they are (hopefully) not that common */
	  push_new_const(L, pReply->str, pReply->len, REDIS_REPLY_ERROR);
	  break;

	case REDIS_REPLY_INTEGER:
	  lua_pushinteger(L, pReply->integer);
	  break;

	case REDIS_REPLY_NIL:
	  lua_pushvalue(L, lua_upvalueindex(1)); /* module table */
	  lua_getfield(L, -1, LUAHIREDIS_KEY_NIL);
	  lua_remove(L, -2); /* module table */
	  break;

	case REDIS_REPLY_STRING:
		DEBUG_PRINT("str=%s\tlen=%d\r\n", pReply->str, pReply->len);
	  lua_pushlstring(L, pReply->str, pReply->len);
	  break;

	case REDIS_REPLY_ARRAY:
	{
	  unsigned int i = 0;

	  lua_createtable(L, pReply->elements, 0);

	  for (i = 0; i < pReply->elements; ++i)
	  {
		/*
		* Not controlling recursion depth:
		* if we parsed the reply somehow,
		* we hope to be able to push it.
		*/

		push_reply(L, pReply->element[i]);
		lua_rawseti(L, -2, i + 1); /* Store sub-reply */
	  }
	  break;
	}

	default: /* should not happen */
	  return luaL_error(L, "command: unknown reply type: %d", pReply->type);
  }

  /*
  if (lua_gettop(L) != base + 1)
  {
	return luaL_error(L, "pushreplystackerror: actual %d expected %d base %d type %d", lua_gettop(L), base + 1, base, pReply->type);
  }
  */

  /*
  * Always returning a single value.
  * If changed, change REDIS_REPLY_ARRAY above.
  */
  return 1;
}


bool parse_redis_protocol(struct request *req)
{
	int         ret;
	void        *reply;    
	redisReader *reader;   
	
	if(strncmp(req->buf, "$-1", 3) == 0)
	{
		lua_pushboolean(req->vm, false);
		return true;
	}
	else if(strncmp(req->buf, "+OK", 3) == 0)
	{
		lua_pushboolean(req->vm, true);
		return true;
	}
	
	reader = redisReaderCreate();
	
	DEBUG_PRINT("req->buf=%s\r\n", req->buf);
	redisReaderFeed(reader, req->buf, sdslen(req->buf));
	ret = redisReaderGetReply(reader,&reply);
	
	if (ret == REDIS_ERR) 
	{
		freeReplyObject(reply);    
		redisReaderFree(reader);
		return false;
	}
	
	
	push_reply(req->vm, (redisReply*)reply);
	
	//printf("%s\r\n", ((redisReply*)reply)->str);
	freeReplyObject(reply);    
	redisReaderFree(reader);
	
	return true;
}

/* Format a command according to the Redis protocol. This function
 * takes a format similar to printf:
 *
 * %s represents a C null terminated string you want to interpolate
 * %b represents a binary safe string
 *
 * When using %b you need to provide both the pointer to the string
 * and the length in bytes. Examples:
 *
 * len = redisFormatCommand(target, "GET %s", mykey);
 * len = redisFormatCommand(target, "SET %s %b", mykey, myval, myvallen);
 */
int redisFormatCommand(char **target, const char *format, ...) 
{
	va_list ap;
	int len;
	va_start(ap,format);
	len = redisvFormatCommand(target,format,ap);
	va_end(ap);
	return len;
}

static void redis_pack_command(lua_State *vm, const char *id, char *cmd, int len)
{
	int sockfd;
	struct request *req, *link;
	
	link = linkpool_get(id);
	if(link == NULL)
	{
		return ;
	}
	
	lua_getglobal(vm, "__USERDATA__"); 
	sockfd  = luaL_checkinteger(vm, -1);   
	lua_pop(vm, 1);  
	
	req = &srv.req[sockfd];
	
	req->link     = link;
	req->state    = REDIS_SEND;
	req->handler  = sockio_write;
	req->buf      = sdscpylen(req->buf, cmd, len);
	
	srv.el->insert(req->link->fd, req, FDEVENT_WRITE);
}


int redis_del(lua_State *vm)
{
	char  *cmd;
	sds   str;
	int   len, argc, i;
	const char  *id, *key;

	argc  = lua_gettop(vm);
	argc += 1;
	id    = luaL_checkstring(vm, 1);
	str   = sdsnew("DEL");
	
	for(i=2; i<argc; i++)
	{
		key = luaL_checkstring(vm, i);
		str = sdscatprintf(str, " %s", key);
	}
	
	len = redisFormatCommand(&cmd, "%s", str);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	sdsfree(str);
	cmd = NULL;
	
	return lua_yield(vm, 0);
}

int redis_incr(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	
	len = redisFormatCommand(&cmd, "INCR %s", key);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	cmd = NULL;
	return lua_yield(vm, 0);
}

int redis_rpush(lua_State *vm)
{
	char  *cmd;
	sds   str;
	int   len, argc, i;
	const char  *id, *key;

	argc  = lua_gettop(vm);
	argc += 1;
	id    = luaL_checkstring(vm, 1);
	str   = sdsnew("RPUSH");
	
	for(i=2; i<argc; i++)
	{
		key = luaL_checkstring(vm, i);
		str = sdscatprintf(str, " %s", key);
	}
	
	len = redisFormatCommand(&cmd, "%s", str);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	sdsfree(str);
	cmd = NULL;
	
	return lua_yield(vm, 0);
}

int redis_lpush(lua_State *vm)
{
	char  *cmd;
	sds   str;
	int   len, argc, i;
	const char  *id, *key;

	argc  = lua_gettop(vm);
	argc += 1;
	id    = luaL_checkstring(vm, 1);
	str   = sdsnew("LPUSH");
	
	for(i=2; i<argc; i++)
	{
		key = luaL_checkstring(vm, i);
		str = sdscatprintf(str, " %s", key);
	}
	
	len = redisFormatCommand(&cmd, "%s", str);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	sdsfree(str);
	cmd = NULL;
	
	return lua_yield(vm, 0);
}

int redis_lindex(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	const char  *ind   = luaL_checkstring(vm, 3);
	
	len = redisFormatCommand(&cmd, "LINDEX %s %s", key, ind);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	cmd = NULL;
	return lua_yield(vm, 0);
}

int redis_lpop(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	
	len = redisFormatCommand(&cmd, "LPOP %s", key);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	cmd = NULL;
	return lua_yield(vm, 0);
}

int redis_llen(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	
	len = redisFormatCommand(&cmd, "LLEN %s", key);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	cmd = NULL;
	return lua_yield(vm, 0);
}

int redis_decr(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	
	len = redisFormatCommand(&cmd, "DECR %s", key);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	cmd = NULL;
	return lua_yield(vm, 0);
}

int redis_select(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	
	len = redisFormatCommand(&cmd, "SELECT %s", key);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	cmd = NULL;
	return lua_yield(vm, 0);
}


int redis_set(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char     *id    = luaL_checkstring(vm, 1);
	const char     *key   = luaL_checkstring(vm, 2);
	const char     *value = luaL_checkstring(vm, 3);
	
	
	len = redisFormatCommand(&cmd, "SET %s %s", key, value);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);
	return lua_yield(vm, 0);
}

int redis_get(lua_State *vm)
{
	int   len;
	char  *cmd;
	const char  *id    = luaL_checkstring(vm, 1);
	const char  *key   = luaL_checkstring(vm, 2);
	
	len  = redisFormatCommand(&cmd, "GET %s", key);
	redis_pack_command(vm, id, cmd, len);
	zfree(cmd);

	return lua_yield(vm, 0);
}

