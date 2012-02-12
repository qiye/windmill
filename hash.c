/* Copyright 2006 David Crawshaw, released under the new BSD license.
 * Version 2, from http://www.zentus.com/c/hash.html */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "hash.h"
#include "zmalloc.h"


typedef uint32_t ub4;   // unsigned 4-byte quantities

#undef get32bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get32bits(d) (*((const uint32_t *) (d)))
#endif

#if !defined (get32bits)
#define get32bits(d) (  (uint32_t)(d)[0]                \
					 + ((uint32_t)(d)[1]<<UINT32_C(8))  \
					 + ((uint32_t)(d)[2]<<UINT32_C(16)) \
					 + ((uint32_t)(d)[3]<<UINT32_C(24)) )
#endif


#define mix(a,b,c) \
{ \
	a -= b; a -= c; a ^= (c>>13); \
	b -= c; b -= a; b ^= (a<<8); \
	c -= a; c -= b; c ^= (b>>13); \
	a -= b; a -= c; a ^= (c>>12);  \
	b -= c; b -= a; b ^= (a<<16); \
	c -= a; c -= b; c ^= (b>>5); \
	a -= b; a -= c; a ^= (c>>3);  \
	b -= c; b -= a; b ^= (a<<10); \
	c -= a; c -= b; c ^= (b>>15); \
}


static const uint32_t sizes[] = {
	53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
	196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
	50331653, 100663319, 201326611, 402653189, 805306457, 1610612741
};
static const int sizes_count = sizeof(sizes) / sizeof(sizes[0]);
static const float load_factor = 0.65;

struct record 
{
	uint32_t hash;
	const char *key;
	void *value;
};

struct hash 
{
	struct record *records;
	uint32_t records_count;
	uint32_t size_index;
};

static int hash_grow(hash *h)
{
	int i;
	struct record *old_recs;
	uint32_t old_recs_length;

	old_recs_length = sizes[h->size_index];
	old_recs = h->records;

	if (h->size_index == sizes_count - 1) return -1;
	if ((h->records = zcalloc(sizes[++h->size_index], sizeof(struct record))) == NULL) 
	{
		h->records = old_recs;
		return -1;
	}

	h->records_count = 0;

	// rehash table
	for (i=0; i < old_recs_length; i++)
		if (old_recs[i].hash && old_recs[i].key)
			hash_add(h, old_recs[i].key, old_recs[i].value);

	zfree(old_recs);

	return 0;
}

static uint32_t hashBobJenkinsUpdate(const char * k, int length, uint32_t initval) 
{
	register ub4 a,b,c,len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
	c = initval;         /* the previous hash value */

   /*---------------------------------------- handle most of the key */
	while (len >= 12) 
	{
		a += get32bits (k);
		b += get32bits (k+4);
		c += get32bits (k+8);
		mix(a,b,c);
		k += 12; len -= 12;
   }

   //------------------------------------- handle the last 11 bytes 
	c += length;
	switch(len) 
	{
		/* all the case statements fall through */
		case 11: c+=((ub4)k[10]<<24);
		case 10: c+=((ub4)k[9]<<16);
		case 9 : c+=((ub4)k[8]<<8);
		// the first byte of c is reserved for the length 
		case 8 : b+=((ub4)k[7]<<24);
		case 7 : b+=((ub4)k[6]<<16);
		case 6 : b+=((ub4)k[5]<<8);
		case 5 : b+=k[4];
		case 4 : a+=((ub4)k[3]<<24);
		case 3 : a+=((ub4)k[2]<<16);
		case 2 : a+=((ub4)k[1]<<8);
		case 1 : a+=k[0];
		/* case 0: nothing left to add */
	}
	mix(a,b,c);
   
	return c;
}

uint32_t hashBobJenkins (const char * k, int length) 
{
	return hashBobJenkinsUpdate (k, length, 0);
}


hash * hash_new(uint32_t capacity) {
	struct hash *h;
	int i, sind;

	capacity /= load_factor;

	for (i=0; i < sizes_count; i++) 
		if (sizes[i] > capacity) { sind = i; break; }

	if ((h = zmalloc(sizeof(struct hash))) == NULL) return NULL;
	if ((h->records = zcalloc(sizes[sind], sizeof(struct record))) == NULL) 
	{
		zfree(h);
		return NULL;
	}

	h->records_count = 0;
	h->size_index = sind;

	return h;
}

void hash_destroy(hash *h)
{
	zfree(h->records);
	zfree(h);
}

int hash_add(hash *h, const char *key, void *value)
{
	struct record *recs;
	int rc;
	uint32_t off, ind, size, code;

	if (key == NULL || *key == '\0') return -2;
	if (h->records_count > sizes[h->size_index] * load_factor) {
		rc = hash_grow(h);
		if (rc) return rc;
	}

	code = hashBobJenkins(key, strlen(key));
	recs = h->records;
	size = sizes[h->size_index];

	ind = code % size;
	off = 0;

	while (recs[ind].key)
		ind = (code + (int)pow(++off,2)) % size;

	recs[ind].hash  = code;
	recs[ind].key   = key;
	recs[ind].value = value;

	h->records_count++;

	return 0;
}

void hash_foreach(hash *h, WPF *handler)
{
	int i;
	struct record *recs;
	uint32_t      recs_length;

	recs_length = sizes[h->size_index];
	recs        = h->records;
	
	for (i=0; i < recs_length; i++)
		if (recs[i].hash && recs[i].key)
			handler(recs[i].key, recs[i].value);
}

void * hash_get(hash *h, const char *key)
{
	struct record *recs;
	uint32_t off, ind, size;
	uint32_t code = hashBobJenkins(key, strlen(key));

	recs = h->records;
	size = sizes[h->size_index];
	ind = code % size;
	off = 0;

	// search on hash which remains even if a record has been removed,
	// so hash_remove() does not need to move any collision records
	while (recs[ind].hash) 
	{
		if ((code == recs[ind].hash) && recs[ind].key && strcmp(key, recs[ind].key) == 0)
		{
			return recs[ind].value;
		}
		ind = (code + (int)pow(++off,2)) % size;
	}

	return NULL;
}

bool hash_remove(hash *h, const char *key)
{
	uint32_t code = hashBobJenkins(key, strlen(key));
	struct record *recs;
	void * value;
	uint32_t off, ind, size;

	recs = h->records;
	size = sizes[h->size_index];
	ind = code % size;
	off = 0;

	while (recs[ind].hash) 
	{
		if ((code == recs[ind].hash) && recs[ind].key && strcmp(key, recs[ind].key) == 0) 
		{
			// do not erase hash, so probes for collisions succeed
			value = recs[ind].value;
			recs[ind].key = 0;
			recs[ind].value = 0;
			h->records_count--;
			return true;
		}
		ind = (code + (int)pow(++off, 2)) % size;
	}
 
	return false;
}

uint32_t hash_size(hash *h)
{
	return h->records_count;
}
