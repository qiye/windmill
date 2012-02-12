#ifndef __POOL_H__
#define __POOL_H__

struct pool_t
{
	struct llhead   free;
	uint16_t        free_size;

	struct sockaddr_in  addr;
	char                *id;
};

void pool_init(struct pool_t *pool);

struct request *linkpool_get(const char *id);

void linkpool_free(struct request *req);

void linkpool_close(int fd, struct request *req);

#endif
