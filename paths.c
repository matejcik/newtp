#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "commands.h"
#include "log.h"
#include "paths.h"
#include "tools.h"

#define MAXHANDLES 16384
struct handle * handles[MAXHANDLES];

int handle_init ()
{
	for (int i = 0; i < MAXHANDLES; i++) handles[i] = NULL;
	return MAXHANDLES;
}

/* share operations */
#define SHARE_HT_MOD 41
#define SHARE_HT_SLOT 5
#define SHARE_HT_LEN (SHARE_HT_MOD * SHARE_HT_SLOT)
struct share * share_ht = NULL;

int share_hash (char const * name, int nlen)
{
	int hash = 0;
	for (int i = 0; i < nlen; hash += name[i++]);
	return hash % SHARE_HT_MOD;
}

int share_add (char const * name, char const * path, int writable)
{
	int nlen = strlen(name);
	int plen = strlen(path);
	int hash = share_hash(name, nlen);
	struct share * shptr;

	if (!share_ht) share_ht = xmalloc(SHARE_HT_LEN * sizeof(struct share)); /* initialize */
	/* find first available position in hashtable */
	shptr = share_ht + hash * SHARE_HT_SLOT;
	while (shptr->used) {
		if (shptr->nlen == nlen && !strncmp(shptr->name, name, nlen)) return 0; /* duplicate entry */
		shptr++;
		if (shptr == share_ht + hash * SHARE_HT_SLOT) return 0; /* table is COMPLETELY FULL! */
		if (shptr - share_ht >= SHARE_HT_LEN) shptr = share_ht; /* wrap around */
	}
	shptr->used = 1;
	shptr->name = xmalloc(nlen + 1);
	strncpyz(shptr->name, name, nlen);
	shptr->nlen = nlen;
	shptr->path = xmalloc(plen + 1);
	shptr->plen = plen;
	strncpyz(shptr->path, path, plen);
	shptr->writable = writable;
	return 1;
}

struct share * share_find (char const * name, int nlen)
{
	if (!share_ht) return NULL; /* no shares :( */
	int hash = share_hash(name, nlen);
	struct share * shptr = share_ht + hash * SHARE_HT_SLOT;
	while (shptr->used) {
		if (shptr->nlen == nlen && !strncmp(shptr->name, name, nlen)) return shptr; /* FOUND! */
		shptr++;
		if (shptr == share_ht + hash * SHARE_HT_SLOT) return NULL; /* got back to start and found nothing */
		if (shptr - share_ht >= SHARE_HT_LEN) shptr = share_ht; /* wrap around */
	}
	return NULL;
}

struct share * share_next (struct share * seed)
{
	/* nothing */
	if (!share_ht) return NULL;
	/* seed out of range */
	if (seed && (seed < share_ht || seed - share_ht >= SHARE_HT_LEN)) return NULL;

	if (seed) seed++;
	else seed = share_ht;

	while (seed - share_ht < SHARE_HT_LEN) {
		if (seed->used) return seed;
		seed++;
	}
	return NULL;
}

void delhandle (struct handle * handle)
{
	int c;
	if (handle->path && *handle->path)
		free(handle->path); /* might be constant "" */
	free(handle->name);
	if (handle->dir) closedir(handle->dir);
	if (handle->fd) RETRY1(c, close(handle->fd));
	free(handle);
}

int handle_assign (uint16_t handle, char const * buf, int len)
{
#define SLASH 1
#define DOT1 2
#define DOT2 3
#define OTHER 0
	int state = SLASH;
	char const * c;
	int pos = 0;
	int shlen = -1; /* share len */

	if (handle >= MAXHANDLES) return ERR_BADHANDLE;

	if (len == 0) {
		/* do nothing */
	} else if (len == 1 && *buf == '/') { /* "/", forbid */
		return ERR_BADPATH;
	} else if (*buf != '/') { /* missing leading slash, forbid */
		return ERR_BADPATH;
	} else { /* skip leading slash */;
		++buf; --len;
	}

	c = buf;
	while (pos < len) {
		if (!*c) return ERR_BADPATH;
		if (state == SLASH && *c == '/') return ERR_BADPATH;
		if (*c == '/' && shlen == -1) shlen = c - buf;

		if (state == DOT1 && *c == '/') return ERR_BADPATH;
		if (state == DOT2 && *c == '/') return ERR_BADPATH;
		if (*c == '/') state = SLASH;
		else if (state == SLASH && *c == '.') state = DOT1;
		else if (state == DOT1 && *c == '.') state = DOT2;
		else state = OTHER;

		++pos; ++c;
	}
	if (state == DOT1 || state == DOT2) return ERR_BADPATH;
	if (shlen == -1) shlen = len;
	/* path is OK, proceed */

	if (handles[handle]) delhandle(handles[handle]);
	handles[handle] = NULL; /* just to be sure */

	struct handle * h = xmalloc(sizeof(struct handle));

	h->name = xmalloc(len + 1);
	strncpyz(h->name, buf, len);
	h->sharelen = shlen;
	h->pathlen = len - shlen;

	h->fd = -1;

	handles[handle] = h;
	logp("handle %d is now \"%s\"", handle, h->name);
	return STAT_OK;
}

struct handle * handle_get (uint16_t handle)
{
	if (handle >= MAXHANDLES) return NULL;
	struct handle * h = handles[handle];
	if (h == NULL) return NULL;

	if (!h->name[0]) { /* root - special case */
		h->path = "";
		h->writable = 0;
		return h;
	}

	/* XXX all the following assumes that shares can change at any time.
	TODO remains to put in some concurrency checks */

	/* do we know a share? */
	if (!h->share || h->sharelen != h->share->nlen || strncmp(h->name, h->share->name, h->sharelen)) {
		h->share = share_find(h->name, h->sharelen);
		if (!h->share) { /* no share exists */
			free(h->path);
			h->path = NULL;
			h->writable = 0;
			return h;
		}
	}
	/* ok, how about the path */
	if (!h->path || strncmp(h->path, h->share->path, h->share->plen)) {
		/* if not exists, or not valid */
		free(h->path);
		h->plen = h->share->plen + h->pathlen;
		h->path = xmalloc(h->plen + 1);
		strncpy(h->path, h->share->path, h->share->plen);
		strncpyz(h->path + h->share->plen, h->name + h->sharelen, h->pathlen);
		/* path changed, make sure we close filehandle */
		int e;
		RETRY1(e, close(h->fd));
		h->fd = -1;
	}
	/* we only checked the share part the rest -should- remain untouched */
	if (h->path[h->share->plen]) {
		assert(h->path[h->share->plen] == '/');
		assert(!strncmp(h->path + h->share->plen, h->name + h->sharelen, h->pathlen));
	} /* else we are working with share's root */
	
	h->writable = h->share->writable;
	return h;
}
