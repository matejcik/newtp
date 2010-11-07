#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
#include "operations.h"
#include "paths.h"
#include "structs.h"
#include "tools.h"

/* 1MB in hex */
#define MAXDATA 0x100000
#define MAXBUFFER 0x500000
/* 128kB in hex */
#define DIRBUFFER 0x20000

/* to be used with recv_* and send_* only. file-related syscalls must handle their errors */
#define MAYBE_RET(what) { \
	int HANDLEret = what; \
	if (HANDLEret <= 0) return HANDLEret; \
}

/* read as "maybe return, freeing (buf) in the process */
#define MAYBE_RET_FREEING(buf, what) { \
	int HANDLEret = what; \
	if (HANDLEret <= 0) { \
		free(buf); \
		return HANDLEret; \
	} \
}

/* "return, freeing (buf)" */
#define RET_FREEING(buf, what) { \
	int ret = what; \
	free(buf); \
	return ret; \
}

#define VALIDATE_HANDLE(h, sock, cmd) { \
	h = handle_get(cmd->handle); \
	if (!h) return send_reply_p(sock, cmd->id, STAT_BADHANDLE); \
	if (!h->path) return send_reply_p(sock, cmd->id, STAT_NOTFOUND); \
}

/***** directory listing functions *****/

int fill_stat (char const * path, struct dir_entry * entry, int writable)
{
	/* caller is responsible for filling entry->name
	and entry->len, they probably know better anyway */
	struct stat st;
	int ret;

	ret = stat(path, &st);
	if (ret == -1) { /* failed to stat */
		entry->type = ENTRY_BAD;
	} else {
		if (S_ISREG(st.st_mode)) entry->type = ENTRY_FILE;
		else if (S_ISDIR(st.st_mode)) entry->type = ENTRY_DIR;
		else entry->type = ENTRY_OTHER;

		if (entry->type == ENTRY_FILE) entry->size = st.st_size;
		else entry->size = 0;

		entry->perm = 0;
		/* XXX more detailed check for access() failure? */
		if (!access(path, R_OK | (entry->type == ENTRY_DIR ? X_OK : 0))) entry->perm |= PERM_READ;
		if (writable && !access(path, W_OK)) entry->perm |= PERM_WRITE;
	}
	return ret;
}

int fill_entry (char const * directory, int dlen, struct dirent const * dirent, struct dir_entry * entry, int writable)
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
	strncpyz(entry->name, dirent->d_name, len);

	path = xmalloc(dlen + 1 + len + 1);
	strncpy(path, directory, dlen);
	path[dlen] = '/';
	strncpyz(path + dlen + 1, dirent->d_name, len);
	
	fill_stat(path, entry, writable);

	free(path);

	return SIZEOF_dir_entry - sizeof(char*) + entry->len;
}

/**** actual command implementations ****/

int cmd_assign (int sock, struct command *cmd)
{
	char * buf = xmalloc(MAXDATA);
	int len;

	MAYBE_RET_FREEING(buf, recv_length(sock, &len));
	if (len > 0) {
		if (len <= MAXDATA) {
			MAYBE_RET_FREEING(buf, recv_full(sock, buf, len));
		} else {
			/* fail, kick the connection */
			free(buf);
			return -1;
		}
	} /* else this is attempt to assign zero-length handle, which is perfectly OK */
	
	RET_FREEING(buf, send_reply_p(sock, cmd->id, handle_assign(cmd->handle, buf, len)));
}

int list_shares (int sock, struct command *cmd)
{
	char * buf = xmalloc(DIRBUFFER);
	int len = DIRBUFFER;
	int filled = 0;
	struct share * share = NULL;
	log("listing root");
	while ((share = share_next(share))) {
		int perm = 0;
		int elen = SIZEOF_dir_entry - sizeof(char*) + share->nlen;
		if (filled + elen > len) {
			len *= 2;
			buf = xrealloc(buf, len);
		}
		if (!access(share->path, R_OK | X_OK)) perm |= PERM_READ;
		if (share->writable && !access(share->path, W_OK)) perm |= PERM_WRITE;
		/* TODO account for non-directory shares (individual files, devices perhaps??) */
		pack(buf + filled, FORMAT_dir_entry, share->nlen, share->name, ENTRY_DIR, perm, (uint64_t)0);
		filled += elen;
	}
	MAYBE_RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_OK));
	RET_FREEING(buf, send_data(sock, buf, filled));
}

int cmd_list (int sock, struct command *cmd)
{
	struct handle * h;
	VALIDATE_HANDLE(h, sock, cmd);
	logp("CMD_LIST %d (%s)", cmd->handle, h->path);

	if (!h->path[0]) return list_shares(sock, cmd);

	if (h->dir) closedir(h->dir);
	h->dir = opendir(h->path);
	if (!h->dir) {
		int err;
		if (errno == EACCES) err = STAT_DENIED;
		else if (errno == ENOENT) err = STAT_NOTFOUND;
		else if (errno == ENOTDIR) err = STAT_NOTDIR;
		else if (errno == EMFILE || errno == ENFILE) /* TODO handle this better */ err = STAT_BADHANDLE;
		else err = STAT_SERVFAIL;

		return send_reply_p(sock, cmd->id, err);
	}
	h->entry.len = 0;
	free(h->entry.name);
	h->entry.name = NULL;
	return cmd_list_cont(sock, cmd);
}

int cmd_list_cont (int sock, struct command *cmd)
{
	char * buffer;
	int filled = 0;
	struct dirent * dirent;
	struct handle * h;
	VALIDATE_HANDLE(h, sock, cmd);
	logp("listing %d (%s)", cmd->handle, h->path);

	/* continues listing on preinitialized dir handle */
	if (!h->dir) {
		log("invalid continuation");
		return send_reply_p(sock, cmd->id, STAT_NOCONTINUE);
	}

	buffer = xmalloc(DIRBUFFER);
	if (h->entry.name) { /* append carried-over entry */
		assert(h->elen < DIRBUFFER); /* XXX handle this better? meh */
		pack(buffer + filled, FORMAT_dir_entry,
			h->entry.len, h->entry.name,
			h->entry.type, h->entry.perm, h->entry.size);
		filled += h->elen;
	}
	while ((dirent = readdir(h->dir))) {
		/* skip "." and ".." */
		if (dirent->d_name[0] == '.')
			if (dirent->d_name[1] == 0 || (dirent->d_name[1] == '.' && dirent->d_name[2] == 0)) continue;
		/* fill the entry */
		h->elen = fill_entry(h->path, h->plen, dirent, &h->entry, h->writable);
		if (h->entry.type == ENTRY_BAD) continue;
		if (filled + h->elen < DIRBUFFER) {
			pack(buffer + filled, FORMAT_dir_entry,
				h->entry.len, h->entry.name,
				h->entry.type, h->entry.perm, h->entry.size);
			filled += h->elen;
		} else {
			MAYBE_RET_FREEING(buffer, send_reply_p(sock, cmd->id, STAT_CONT));
			MAYBE_RET_FREEING(buffer, send_data(sock, buffer, filled));
			free(buffer);
			logp("sent %d bytes continued", filled);
			return filled;
		}
	}
	/* we got to the end */
	MAYBE_RET_FREEING(buffer, send_reply_p(sock, cmd->id, STAT_OK));
	MAYBE_RET_FREEING(buffer, send_data(sock, buffer, filled));
	free(buffer);
	free(h->entry.name);
	h->entry.name = NULL;
	closedir(h->dir);
	h->dir = NULL;
	logp("sent %d bytes", filled);
	return filled;
}

int cmd_stat (int sock, struct command *cmd)
{
	struct handle * h;
	struct dir_entry entry;
	int err;

	VALIDATE_HANDLE(h, sock, cmd);
	logp("CMD_STAT %d (%s)",  cmd->handle, h->path);

	err = fill_entry(h->path, &entry, h->writable);

	MAYBE_RET(send_reply_p(sock, cmd->id, STAT_OK));
}

int cmd_read (int sock, struct command *cmd)
{
	struct handle * h;
	int err;
	int done = 0;
	char * buf;
	struct params_offlen params;

	/* be a good guy and pick parameters first */
	MAYBE_RET(recv_params_offlen(sock, &params));

	VALIDATE_HANDLE(h, sock, cmd);
	logp("CMD_READ %d (%s): ofs %llu, len %d", cmd->handle, h->path, params.offset, params.length);

	if (h->fd > 0 && h->open_w) {
		close(h->fd);
		h->fd = -1;
	}
	if (h->fd == -1) {
		RETRY1(h->fd, open(h->path, O_RDONLY));
		err = STAT_OK;
		if (h->fd == -1) { /* open failed */
			if (errno == EACCES) err = STAT_DENIED;
			else if (errno == ENOENT) err = STAT_NOTFOUND;
			else if (errno == ENOTDIR) err = STAT_BADPATH;
			else err = STAT_SERVFAIL;
			return send_reply_p(sock, cmd->id, err);
		}
		h->open_w = 0;
	}
	/* TODO check whether the open handle belongs to the correct path */
	RETRY1(err, lseek(h->fd, params.offset, SEEK_SET));
	if (err == -1) {
		/* something bad went wronger */
		return send_reply_p(sock, cmd->id, STAT_SERVFAIL);
	}

	if (params.length > MAXBUFFER) params.length = MAXBUFFER;
	assert(params.length < SSIZE_MAX); /* "If count is greater than SSIZE_MAX, the result is unspecified." */
	buf = xmalloc(params.length);
	/* manual retry-loop */
	while (done < params.length) {
		err = read(h->fd, buf + done, params.length - done);
		if (err == -1) {
			if (errno == EINTR) continue;
			else if (errno == EISDIR || errno == EFAULT) {
				RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_NOTFILE));
			} else if (errno == EIO) {
				if (done > 0) /* return short read */ break;
				else RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_IO));
			} else {
				RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_SERVFAIL));
			}
		} else if (err == 0) {
			/* EOF - short read */
			break;
		} else {
			done += err;
		}
	}
	/* we have read all the requested data */
	MAYBE_RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_OK));
	RET_FREEING(buf, send_data(sock, buf, done));
}

#define MIN(a,b) ((a) > (b) ? (b) : (a))
int skip_data(int sock, int len)
{
	char buf[8192];
	while (len > 0) {
		MAYBE_RET(recv_full(sock, buf, MIN(len,8192)));
		len -= 8192;
	}
	return 1;
}

int cmd_write (int sock, struct command *cmd)
{
	struct handle * h;
	int err;
	int done = 0, total = 0;
	int len;
	char * buf;
	struct params_offlen params;

	/* be a good guy and pick parameters first */
	MAYBE_RET(recv_params_offlen(sock, &params));

	/* TODO make sure that handles to "files" in "root" return STAT_DENIED instead of NOTFOUND */
	VALIDATE_HANDLE(h, sock, cmd);
	logp("CMD_WRITE %d (%s): ofs %llu, len %d", cmd->handle, h->path, params.offset, params.length);

	if (!h->writable) return send_reply_p(sock, cmd->id, STAT_DENIED);

	if (h->fd > 0 && !h->open_w) {
		close(h->fd);
		h->fd = -1;
	}
	if (h->fd == -1) {
		RETRY1(h->fd, open(h->path, O_CREAT | O_WRONLY, 0644)); /* TODO think about the mode some more */
		err = STAT_OK;
		if (h->fd == -1) { /* open failed */
			if (errno == EACCES) err = STAT_DENIED;
			else if (errno == EISDIR) err = STAT_NOTFILE;
			else if (errno == ENOENT) err = STAT_NOTFOUND;
			else if (errno == ENOTDIR) err = STAT_BADPATH;
			else err = STAT_SERVFAIL;
			/* XXX maybe we should send_reply_p before skip_data */
			MAYBE_RET(skip_data(sock, params.length));
			return send_reply_p(sock, cmd->id, err);
		}
		h->open_w = 1;
	}

	/* we created the file if we could, now we can return in case of zero write */
	if (params.length == 0) {
		if (fsync(h->fd) == -1) {
			return send_reply_p(sock, cmd->id, STAT_IO);
		} else {
			MAYBE_RET(send_reply_p(sock, cmd->id, STAT_OK));
			return send_length(sock, 0);
		}
	}

	/* TODO check whether the open handle belongs to the correct path */
	RETRY1(err, lseek(h->fd, params.offset, SEEK_SET));
	if (err == -1) {
		/* something bad went wronger */
		MAYBE_RET(skip_data(sock, params.length));
		return send_reply_p(sock, cmd->id, STAT_SERVFAIL);
	}

	len = MIN(params.length, MAXBUFFER);
	buf = xmalloc(len);
	/* manual retry-loop */
	while (params.length > 0) {
		len = MIN(params.length, MAXBUFFER);
		done = 0;
		MAYBE_RET_FREEING(buf, recv_full(sock, buf, len));
		while (done < len) {
			err = write(h->fd, buf + done, len - done);
			if (err == -1) {
				if (errno == EINTR) continue;
				if (total > 0) {
					MAYBE_RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_PARTIAL));
					RET_FREEING(buf, send_length(sock, total));
				} else if (errno == EIO) {
					RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_IO));
				} else if (errno == ENOSPC) {
					RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_FULL));
				} else {
					RET_FREEING(buf, send_reply_p(sock, cmd->id, STAT_SERVFAIL));
				}
			} else {
				done += err;
				total += err;
			}
		}
		params.length -= len;
	}
	/* we have received and written all the requested data */
	free(buf);
	return send_reply_p(sock, cmd->id, STAT_OK);
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

	/* intro */
	whoami.version = 0;
	whoami.maxhandles = handle_init();
	whoami.maxdata = MAXDATA;

	return send_intro(sock, &whoami);
}
