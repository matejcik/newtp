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

int check_path (char const * buf, int len)
{
	enum {SLASH, DOT1, DOT2, OTHER} state = SLASH;
	int pos = 0;

	if (len == 0) {
		return 1;
	} else if (len == 1 && *buf == '/') { /* "/", forbid */
		return 0;
	} else if (*buf != '/') { /* missing leading slash, forbid */
		return 0;
	} else { /* skip leading slash */;
		++buf; pos = 1;
	}

	while (pos < len) {
		if (!*buf) return 0;
		if (state == SLASH && *buf == '/') return 0;
		if (state == DOT1 && *buf == '/') return 0;
		if (state == DOT2 && *buf == '/') return 0;
		if (*buf == '/') state = SLASH;
		else if (state == SLASH && *buf == '.') state = DOT1;
		else if (state == DOT1 && *buf == '.') state = DOT2;
		else state = OTHER;

		++pos; ++buf;
	}
	if (state == DOT1 || state == DOT2) return 0;

	return 1;
}

int handle_assign (uint16_t handle, char const * buf, int len)
{
	struct handle * h;
	if (handle >= MAXHANDLES) return ERR_BADHANDLE;
	h = handle_make(buf, len);
	if (!h) return ERR_BADPATH;
	return handle_assign_ptr(handle, h);
}

int handle_assign_ptr (uint16_t handle, struct handle * h)
{
	if (handle >= MAXHANDLES) return ERR_BADHANDLE;
	if (handles[handle]) delhandle(handles[handle]);
	handles[handle] = h;

	logp("handle %d is now \"%s\"", handle, h->name);
	return STAT_OK;
}

struct handle * handle_make (char const * buf, int len)
{
	int shlen = 0;
	if (!check_path(buf, len)) return NULL;
	assert(len != 1);
	if (len > 1) {
		/* leading slash */
		++buf;
		--len;
	}

	while (buf[shlen] != '/' && shlen < len) shlen++;

	struct handle * h = xmalloc(sizeof(struct handle));

	h->name = xmalloc(len + 1);
	strncpyz(h->name, buf, len);
	h->sharelen = shlen;
	h->pathlen = len - shlen;

	h->fd = -1;
	handle_fill_path(h);

	return h;
}

struct handle * handle_get (uint16_t handle)
{
	if (handle >= MAXHANDLES) return NULL;
/*	struct handle * h = handles[handle];
	if (h == NULL) return NULL;*/
	return handles[handle];
}

void handle_fill_path (struct handle * h)
{
	if (h->path) return;

	if (!h->name[0]) { /* root - special case */
		h->path = "";
		h->writable = 0;
		return;
	}

	/* XXX all the following assumes that shares can change at any time.
	 * That might be useful in the future.
	 * TODO remains to put in some concurrency checks */

	/* do we know a share? */
	if (!h->share || h->sharelen != h->share->nlen || strncmp(h->name, h->share->name, h->sharelen)) {
		h->share = share_find(h->name, h->sharelen);
		if (!h->share) { /* no share exists */
			free(h->path);
			h->path = NULL;
			h->writable = 0;
			return;
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
	return;
}
