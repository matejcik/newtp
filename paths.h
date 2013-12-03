#ifndef PATHS__H__
#define PATHS__H__

#include <dirent.h>
#include "structs.h"

struct share {
	int used;
	char * name;
	int nlen;
	char * path;
	int plen;
	int writable;
};

struct handle {
	/* full (not necessarily absolute) filesystem path,
	writable state. info updated in handle_get call. */
	char * path;
	int plen;
	int writable;

	/* assigned path string, lengths, share info */
	char * name;
	int sharelen, pathlen;
	struct share * share;

	/* fd and dir handles */
	int fd;
	int open_w;
	DIR *dir;
	
	/* cache for directory reading */
	char * entry_name;
	int entry_len;
};

/* initialize handles and return maxhandles */
int handle_init ();

/* try to assign the path from buf/len into handle.
return STAT_OK on success, or appropriate
ERR_* error code. */
int handle_assign (uint16_t handle, char const * buf, int len);

/* check whether the given handle is within range and assigned.
update handle path in handle->path and return pointer to handle
or return NULL if the handle is bad. */
struct handle * handle_get (uint16_t handle);


int share_add (char const * name, char const * path, int writable);
/*void share_del (char const * name); maybe later */
struct share * share_find(char const * name, int nlen);
struct share * share_next(struct share * seed);

#endif
