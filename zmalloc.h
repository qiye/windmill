#ifndef __ZMALLOC_H
#define __ZMALLOC_H

#include <jemalloc/jemalloc.h>

#define zfree(ptr)            JEMALLOC_P(free)(ptr)
#define zmalloc(size)         JEMALLOC_P(malloc)(size)
#define zcalloc(num, size)    JEMALLOC_P(calloc)(num, size)
#define zrealloc(ptr, size)   JEMALLOC_P(realloc)(ptr, size)
#define zmalloc_size(p)       JEMALLOC_P(malloc_usable_size)(p)


char *zstrdup (const char *s);

char *zstrndup (const char *s, size_t n);

#endif 
