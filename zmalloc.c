#include "zmalloc.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>


char *zstrdup (const char *s)
{
	size_t len;
	char   *result;
	

	if(s == NULL)
		return NULL;
	
	len = strlen(s);
	result = (char *) zmalloc (len + 1);
	if (!result)
	return NULL;

	result[len] = '\0';
	return (char *) memcpy (result, s, len);
}

char *zstrndup (const char *s, size_t n)
{
	size_t len;
	char   *result;
	

	if(n == 0 || s == NULL)
	{
		return NULL;
	}
	if (n < (len = strlen(s)))
		len = n;

	result = (char *) zmalloc (len + 1);
	if (!result)
	return NULL;

	result[len] = '\0';
	return (char *) memcpy (result, s, len);
}


