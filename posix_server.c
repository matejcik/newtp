#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "commands.h"
#include "common.h"
#include "log.h"
#include "server.h"
#include "structs.h"
#include "tools.h"

struct handle {
	char *name;
	FILE *file;
	int open_rw;
	DIR *dir;

	struct dir_entry entry;
	int elen;
};

#define MAXHANDLES 16
struct handle * handles[MAXHANDLES];

/**** handle functions ****/
struct handle * newhandle(char* name)
{
	struct handle * h = xmalloc(sizeof(struct handle));
	h->name = name;
	h->file = NULL;
	h->open_rw = 0;
	h->dir = NULL;
	return h;
}

void delhandle (struct handle * handle)
{
	free(handle->name);
	if (handle->dir) closedir(handle->dir);
	if (handle->file) fclose(handle->file);
	free(handle);
}

int validate_assign_handle (uint16_t handle, struct data_packet *pkt)
{
#define SLASH 1
#define DOT1 2
#define DOT2 3
#define OTHER 0
	int state = OTHER;
	char *c = pkt->data;
	char *name;
	int pos = 0;

	if (handle >= MAXHANDLES) return STAT_BADHANDLE;

	//if (*c != '/') return STAT_BADPATH;
	while (pos < pkt->len) {
		if (!*c) return STAT_BADPATH;
		if (state == DOT1 && *c == '/') return STAT_BADPATH;
		if (state == DOT2 && *c == '/') return STAT_BADPATH;
		if (state == SLASH && *c == '/') return STAT_BADPATH;
		if (*c == '/') state = SLASH;
		else if (state == SLASH && *c == '.') state = DOT1;
		else if (state == DOT1 && *c == '.') state = DOT2;
		else state = OTHER;

		++pos; ++c;
	}

	if (handles[handle]) delhandle(handles[handle]);
	/* prepend "./", append "\0" */
	name = xmalloc(pkt->len + 3);
	name[0] = '.'; name[1] = '/';
	strncpy(name + 2, pkt->data, pkt->len);
	name[pkt->len + 2] = 0;
	handles[handle] = newhandle(name);
	return STAT_OK;
}

#define VALIDATE_HANDLE(h, sock, cmd) { \
	if (cmd->handle >= MAXHANDLES || handles[cmd->handle] == NULL) { \
		return send_reply_p(sock, cmd->id, STAT_BADHANDLE); \
	} \
	h = handles[cmd->handle]; \
}


/***** directory listing functions *****/

int fill_entry (char const * directory, int dlen, struct dirent const * dirent, struct dir_entry * entry)
{
	char * path;
	int len, ret;
	struct stat st;
	assert(entry); assert(dirent);
	/* entry is pre-allocated, dirent should already be checked for null */

	len = strlen(dirent->d_name);
	if (entry->len < len) {
		free(entry->name); /* entry->name should either be NULL or a malloc'd chunk */
		entry->name = xmalloc(len + 1);
	}
	entry->len = len; /* overwriting the len info, so if there was a preallocated chunk,
		we don't know its size now and we may be reallocating needlessly. still, it's
		better than reallocating every time */
	strncpy(entry->name, dirent->d_name, len + 1);

	path = xmalloc(dlen + 1 + len + 1);
	strncpy(path, directory, dlen);
	path[dlen] = '/';
	strncpy(path + dlen + 1, dirent->d_name, len + 1);

	ret = stat(path, &st);
	if (ret == -1) {
		entry->type = ENTRY_BAD;
	} else {
		if (S_ISREG(st.st_mode)) entry->type = ENTRY_FILE;
		else if (S_ISDIR(st.st_mode)) entry->type = ENTRY_DIR;
		else entry->type = ENTRY_OTHER;

		entry->size = st.st_size;

		entry->perm = 0;
		/* XXX more detailed check for access() failure? */
		if (!access(path, R_OK)) entry->perm |= PERM_READ;
		if (!access(path, W_OK)) entry->perm |= PERM_WRITE;
	}

	return SIZEOF_dir_entry - sizeof(char*) + entry->len;
}

/* to be used with recv_* and send_* only. file-related syscalls must handle their errors */
#define MAYBE_RET(what) { \
	int HANDLEret = what; \
	if (HANDLEret <= 0) return HANDLEret; \
}

/* EINTR retrying macros for functions that return 0/null or -1 on failure */
#define RETRY0(a, what) do { a = what; } while (a == 0 && errno == EINTR)
#define RETRY1(a, what) do { a = what; } while (a == -1 && errno == EINTR)

/**** actual command implementations ****/

int cmd_assign (int sock, struct command *cmd)
{
	struct data_packet pkt;
	if (cmd->handle >= MAXHANDLES) return send_reply_p(sock, cmd->id, STAT_BADHANDLE);

	MAYBE_RET(recv_data(sock, &pkt));
	return send_reply_p(sock, cmd->id, validate_assign_handle(cmd->handle, &pkt));
}

int cmd_list (int sock, struct command *cmd)
{
	struct handle * h;
	VALIDATE_HANDLE(h, sock, cmd);

	if (h->dir) closedir(h->dir);
	h->dir = opendir(h->name);
	if (!h->dir) {
		int err;
		if (errno == EACCES) err = STAT_EACCESS;
		else if (errno == ENOENT) err = STAT_NOTFOUND;
		else if (errno == ENOTDIR) err = STAT_NOTDIR;
		else if (errno == EMFILE || errno == ENFILE) /* TODO handle this better */ err = STAT_BADHANDLE;
		else err = STAT_SERVFAIL;

		return send_reply_p(sock, cmd->id, err);
	}
	h->entry.len = 0;
	h->entry.name = NULL;
	log("init successful");
	return cmd_list_cont(sock, cmd);
}

int cmd_list_cont (int sock, struct command *cmd)
{
	char * buffer;
	int filled = 0;
	int dlen;
	struct dirent * dirent;
	struct handle * h;
	VALIDATE_HANDLE(h, sock, cmd);

	dlen = strlen(h->name);

	/* continues listing on preinitialized dir handle */
	if (!h->dir) {
		log("invalid continuation");
		return send_reply_p(sock, cmd->id, STAT_NOCONTINUE);
	}

#define BUFLEN 128000
	buffer = xmalloc(BUFLEN);
	while ((dirent = readdir(h->dir))) {
		/* skip "." and ".." */
		if (dirent->d_name[0] == '.')
			if (dirent->d_name[1] == 0 || (dirent->d_name[1] == '.' && dirent->d_name[2] == 0)) continue;
		/* fill the entry */
		h->elen = fill_entry(h->name, dlen, dirent, &h->entry);
		if (h->entry.type == ENTRY_BAD) continue;
		if (filled + h->elen < BUFLEN) {
			pack(buffer + filled, FORMAT_dir_entry,
				h->entry.len, h->entry.name,
				h->entry.type, h->entry.perm, h->entry.size);
			filled += h->elen;
		} else {
			MAYBE_RET(send_reply_p(sock, cmd->id, STAT_CONT));
			MAYBE_RET(send_data(sock, buffer, filled));
			free(buffer);
			logp("sent %d bytes continued", filled);
			return filled;
		}
	}
	/* we got to the end */
	MAYBE_RET(send_reply_p(sock, cmd->id, STAT_OK));
	MAYBE_RET(send_data(sock, buffer, filled));
	free(buffer);
	free(h->entry.name);
	closedir(h->dir);
	h->dir = NULL;
	logp("sent %d bytes", filled);
	return filled;
}

int cmd_read (int sock, struct command *cmd)
{
	struct handle * h;
	int err;
	long pos;
	struct params_offlen params;

	/* be a good guy and pick parameters first */
	MAYBE_RET(recv_params_offlen(sock, &params));

	VALIDATE_HANDLE(h, sock, cmd);

	if (!h->file) {
		RETRY0(h->file, fopen(h->name, "r"));
		err = STAT_OK;
		if (!h->file) { /* open failed */
			if (errno == EACCES) err = STAT_EACCESS;
			else if (errno == EISDIR) err = STAT_NOTFILE;
			else if (errno == ENOENT) err = STAT_NOTFOUND;
			else if (errno == ENOTDIR) err = STAT_BADPATH;
			else err = STAT_SERVFAIL;
			return send_reply_p(sock, cmd->id, err);
		}
	}
	/* TODO check whether the open handle belongs to the correct path */
	RETRY1(err, fseek(h->file, params->offset, SEEK_SET));
	if (err == -1) {
		if (errno == EINVAL) {
			/* offset beyond end of file - return an empty read */
			MAYBE_RET(send_reply_p(sock, cmd->id, STAT_OK));
			return send_data(sock, NULL, 0);
		} else {
			/* something bad went wronger */
			return send_reply_p(sock, cmd->id, STAT_SERVFAIL);
		}
	}

}

int cmd_write (int sock, struct command *cmd)
{
	return -1;
}
int cmd_delete (int sock, struct command *cmd)
{
	return -1;
}
int cmd_rename (int sock, struct command *cmd)
{
	return -1;
}

int server_init (int sock)
{
	struct intro whoami;

	/* initialize handles */
	for (int i = 0; i < MAXHANDLES; i++) handles[i] = NULL;

	/* intro */
	whoami.version = 0;
	whoami.maxhandles = MAXHANDLES;
	whoami.handlelen = 4096;

	return send_intro(sock, &whoami);
}
