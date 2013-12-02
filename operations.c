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

#define REPLY(s, len) pack_reply_p(response, cmd->request_id, 0, (s), (len)) + (len)

#define VALIDATE_HANDLE(h) \
	h = handle_get(cmd->handle); \
	if (!h) return REPLY(ERR_BADHANDLE, 0); \
	if (!h->path) return REPLY(ERR_NOTFOUND, 0);

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
	int len;
	assert(entry); assert(dirent);
	/* entry is pre-allocated, dirent should already be checked for null */

	len = strlen(dirent->d_name);
	if (entry->name_len < len) {
		entry->name = xrealloc(entry->name, len + 1); /* entry->name should either be NULL or a malloc'd chunk */
	}
	entry->name_len = len; /* overwriting the len info, so if there was a preallocated chunk,
		we don't know its size anymore and we may be reallocating needlessly. still, it's
		better than reallocating every time */
	strncpyz(entry->name, dirent->d_name, len);

	path = xmalloc(dlen + 1 + len + 1);
	strncpy(path, directory, dlen);
	path[dlen] = '/';
	strncpyz(path + dlen + 1, dirent->d_name, len);
	
	fill_stat(path, entry, writable);

	free(path);

	return SIZEOF_dir_entry(entry);
}

/**** actual command implementations ****/

int cmd_ASSIGN (struct command * cmd, char * payload, char * response)
{
	int res = handle_assign(cmd->handle, payload, cmd->length);
	return REPLY(res, 0);
}

int list_shares (struct command * cmd, char * response)
{
	int filled = sizeof(uint16_t);
	char * buf = response + SIZEOF_reply();
	int entry_len = 0;
	int entries = 0;
	struct dir_entry entry;
	struct share * share = NULL;
	log("listing root");

	/* `buf` is starting at position after the reply packet,
	 * `filled` is size of payload in use, starts at length of entries counter */

	while ((share = share_next(share))) {
		entry.name_len = share->nlen;
		entry.name = share->name;
		/* TODO account for non-directory shares (individual files, devices perhaps??) */
		entry.type = ENTRY_DIR;
		entry.perm = 0;
		if (!access(share->path, R_OK | X_OK)) entry.perm |= PERM_READ;
		if (share->writable && !access(share->path, W_OK)) entry.perm |= PERM_WRITE;
		entry.size = 0;

		entry_len = pack_dir_entry(buf + filled, &entry);
		if (filled + entry_len > MAX_LENGTH) {
			break;
		}
		filled += entry_len;
		++entries;
	}
	/* TODO do something about repeated read */

	/* write number of entries at start of buf */
	pack(buf, "s", entries);
	return REPLY(STAT_FINISHED, filled);
}

int cmd_REWINDDIR (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	VALIDATE_HANDLE(h);
	logp("CMD_REWINDDIR %d (%s)", cmd->handle, h->path);
	
	if (!h->path[0]) return REPLY(STAT_OK, 0);

	if (h->dir) closedir(h->dir);
	h->dir = opendir(h->path);
	if (!h->dir) {
		int err;
		if (errno == EACCES) err = ERR_DENIED;
		else if (errno == ENOENT) err = ERR_NOTFOUND;
		else if (errno == ENOTDIR) err = ERR_NOTDIR;
		else if (errno == EMFILE || errno == ENFILE) /* TODO handle this better */
				err = ERR_BADHANDLE;
		else err = ERR_FAIL;

		return REPLY(err, 0);
	}

	h->entry.name_len = 0;
	free(h->entry.name);
	h->entry.name = NULL;

	return REPLY(STAT_OK, 0);
}

int cmd_READDIR (struct command * cmd, char * payload, char * response)
{
	int filled = sizeof(uint16_t);
	int entries = 0, len = 0;
	int result = STAT_FINISHED;
	char * buf = response + SIZEOF_reply();
	struct dirent * dirent;
	struct handle * h;
	VALIDATE_HANDLE(h);
	logp("CMD_READDIR %d (%s)", cmd->handle, h->path);

	if (!h->path[0]) return list_shares(cmd, response);

	/* continues listing on preinitialized dir handle */
	if (!h->dir) {
		log("invalid continuation");
		return REPLY(ERR_READDIR, 0);
	}

	if (h->entry.name) { /* append carried-over entry */
		filled += pack_dir_entry(buf + filled, &h->entry);
	}

	while ((dirent = readdir(h->dir))) {
		/* skip "." and ".." */
		if (dirent->d_name[0] == '.')
			if (dirent->d_name[1] == 0 || (dirent->d_name[1] == '.' && dirent->d_name[2] == 0)) continue;
		/* fill the entry */
		fill_entry(h->path, h->plen, dirent, &h->entry, h->writable);
		if (h->entry.type == ENTRY_BAD) continue;
		len = pack_dir_entry(buf + filled, &h->entry);
		
		if (filled + len > MAX_LENGTH) {
			result = STAT_CONTINUED;
			break;
		}
		filled += len;
		++entries;
	}

	if (!dirent) {
		/* end of listing, do stuff to directory */
		closedir(h->dir);
		h->dir = 0;
		free(h->entry.name);
		h->entry.name_len = 0;
		h->entry.name = NULL;
	}

	logp("sent %d items, %d bytes", entries, filled);
	pack(buf, "s", entries);
	return REPLY(result, filled);
}

int cmd_STAT (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	struct dir_entry entry;
	int len;

	VALIDATE_HANDLE(h);
	logp("CMD_STAT %d (%s)",  cmd->handle, h->path);

	fill_stat(h->path, &entry, h->writable);
	entry.name_len = 0;
	entry.name = NULL;
	len = pack_dir_entry(response + SIZEOF_reply(), &entry);
	return REPLY(STAT_OK, len); /* TODO tohle opravdu neni dobre */
}

int cmd_READ (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	int err;
	int done = 0;
	char * buf = response + SIZEOF_reply();
	struct params_offlen params;

	/* TODO make sure packet actually contains enough data! */
	unpack_params_offlen(payload, &params);

	VALIDATE_HANDLE(h);
	logp("CMD_READ %d (%s): ofs %llu, len %d", cmd->handle, h->path, (long long unsigned)params.offset, params.length);

	if (h->fd > 0 && h->open_w) {
		close(h->fd);
		h->fd = -1;
	}
	if (h->fd == -1) {
		RETRY1(h->fd, open(h->path, O_RDONLY));
		err = STAT_OK;
		if (h->fd == -1) { /* open failed */
			if (errno == EACCES) err = ERR_DENIED;
			else if (errno == ENOENT) err = ERR_NOTFOUND;
			else if (errno == ENOTDIR) err = ERR_BADPATH;
			else err = ERR_FAIL;
			return REPLY(err, 0);
		}
		h->open_w = 0;
	}
	/* TODO check whether the open handle belongs to the correct path */
	RETRY1(err, lseek(h->fd, params.offset, SEEK_SET));
	if (err == -1) {
		/* something bad went wronger */
		return REPLY(ERR_BADOFFSET, 0);
	}

	/* manual retry-loop */
	while (done < params.length) {
		err = read(h->fd, buf + done, params.length - done);
		if (err == -1) {
			if (errno == EINTR) continue;
			else if (errno == EISDIR || errno == EFAULT) {
				return REPLY(ERR_NOTFILE, done);
			} else if (errno == EIO) {
				return REPLY(ERR_IO, done);
			} else {
				return REPLY(ERR_FAIL, done);
			}
		} else if (err == 0) {
			/* end of file */
			close(h->fd);
			h->fd = -1;
			break;
		} else {
			done += err;
		}
	}
	return REPLY(STAT_OK, done);
}

int cmd_WRITE (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	int err;
	int write_length;
	uint64_t offset;

	/* be a good guy and pick parameters first */
	unpack(payload, "l", &offset);
	write_length = cmd->length - sizeof(uint64_t);

	/* clear response number */
	pack(response + SIZEOF_reply(), "s", 0);

	/* TODO make sure that handles to "files" in "root" return STAT_DENIED instead of NOTFOUND */
	VALIDATE_HANDLE(h);
	logp("CMD_WRITE %d (%s): ofs %llu, len %d", cmd->handle, h->path, (long long unsigned)offset, write_length);

	if (!h->writable) return REPLY(ERR_DENIED, sizeof(uint16_t));

	if (h->fd > 0 && !h->open_w) {
		close(h->fd);
		h->fd = -1;
	}
	if (h->fd == -1) {
		RETRY1(h->fd, open(h->path, O_CREAT | O_WRONLY)); /* TODO think about mode? */
		err = STAT_OK;
		if (h->fd == -1) { /* open failed */
			if (errno == EACCES) err = ERR_DENIED;
			else if (errno == EISDIR) err = ERR_NOTFILE;
			else if (errno == ENOENT) err = ERR_NOTFOUND;
			else if (errno == ENOTDIR) err = ERR_BADPATH;
			else err = ERR_FAIL;
			return REPLY(err, sizeof(uint16_t));
		}
		h->open_w = 1;
	}

	/* we created the file if we could, now we can return in case of zero write */
	if (write_length == 0) {
		if (fsync(h->fd) == -1) {
			return REPLY(ERR_IO, sizeof(uint16_t)); /* TODO only IO? */
		} else {
			return REPLY(STAT_OK, sizeof(uint16_t));
		}
	}

	/* TODO check whether the open handle belongs to the correct path */
	RETRY1(err, lseek(h->fd, offset, SEEK_SET));
	if (err == -1) {
		/* something bad went wronger */
		return REPLY(ERR_FAIL, sizeof(uint16_t));
	}

	/* manual retry-loop */
	payload += sizeof(uint64_t);
	while (write_length > 0) {
		err = write(h->fd, payload, write_length);
		if (err == -1) {
			if (errno == EINTR) continue;
			if (errno == EIO) err = ERR_IO;
			else if (errno == ENOSPC) err = ERR_DEVFULL;
			else err = ERR_FAIL;
			/* where is TOOBIG? TODO */
			return REPLY(err, sizeof(uint16_t));
		} else {
			payload += err;
			write_length -= err;
		}
	}
	/* we have received and written all the requested data */
	return REPLY(STAT_OK, sizeof(uint16_t));
}

int cmd_DELETE (struct command * cmd, char * payload, char * response)
{
	return -1;
}
int cmd_RENAME (struct command * cmd, char * payload, char * response)
{
	return -1;
}
