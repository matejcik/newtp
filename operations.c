#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "commands.h"
#include "common.h"
#include "log.h"
#include "operations.h"
#include "paths.h"
#include "structs.h"
#include "tools.h"

#define LONGEST_PATH 16384

#define REPLY(s, len) pack_reply_p(response, cmd->request_id, 0, (s), (len)) + (len)

#define VALIDATE_HANDLE(h) \
	h = handle_get(cmd->handle); \
	if (!h) return REPLY(ERR_BADHANDLE, 0); \
	if (!h->path) return REPLY(ERR_NOTFOUND, 0);


static int _unpack_succeeded = 1;

int _unpack_or_flag(int unpack_result)
{
	_unpack_succeeded = (unpack_result >= 0);
	return unpack_result;
}

#define DIE_OR(x) \
	_unpack_or_flag(x); \
	if (!_unpack_succeeded) return REPLY(ERR_BADPACKET, 0);

/***** directory listing functions *****/

static inline int attr_size (uint8_t attr)
{
	switch (attr) {
		case ATTR_TYPE:   return 1;
		case ATTR_RIGHTS: return 1;
		case ATTR_SIZE:   return 8;
		case ATTR_DEV_ID: return 4;
		case ATTR_LINKS:  return 4;
		case ATTR_ATIME:  return 8;
		case ATTR_MTIME:  return 8;
		case ATTR_PTYPE:  return 1;
		case ATTR_PERMS:  return 2;
		case ATTR_CTIME:  return 8;
		case ATTR_UID:    return 4;
		case ATTR_GID:    return 4;
		default: return 0;
	}
}

int calculate_attr_len (char const * attr_spec, int len)
{
	char attrs[256];
	int sum = 0;

	for (int i = 0; i < 256; i++) attrs[i] = 0;

	/* clear known attributes */
	attrs[ATTR_TYPE] = 1;
	attrs[ATTR_RIGHTS] = 1;
	attrs[ATTR_SIZE] = 1;
	attrs[ATTR_DEV_ID] = 1;
	attrs[ATTR_LINKS] = 1;
	attrs[ATTR_ATIME] = 1;
	attrs[ATTR_MTIME] = 1;

	attrs[ATTR_PTYPE] = 1;
	attrs[ATTR_PERMS] = 1;
	attrs[ATTR_CTIME] = 1;
	attrs[ATTR_UID] = 1;
	attrs[ATTR_GID] = 1;

	for (int i = 0; i < len; i++) {
		unsigned char a = (unsigned char)attr_spec[i];
		if(!attrs[a]) return -1;
		attrs[a] = 0;
		sum += attr_size(a);
	}

	return sum;
}

int fill_stat (char const * path, int writable, char * attributes, char const * attr_spec, int spec_len)
{
	/* we assume that attributes have the proper size */
	struct stat st;
	int ret;
	unsigned int value;
	uint64_t bigvalue;

	ret = stat(path, &st); /* XXX maybe stat optionally */

	if (ret == -1) return -1;
	
	for (int i = 0; i < spec_len; i++) {
		switch (*attr_spec++) {
			case ATTR_TYPE:
				if (S_ISREG(st.st_mode)) value = TYPE_FILE;
				else if (S_ISDIR(st.st_mode)) value = TYPE_DIR;
				else value = TYPE_OTHER;
				attributes += pack(attributes, "c", (uint8_t)value);
				break;

			case ATTR_RIGHTS:
				value = 0;
				/* XXX more detailed check for access() failure? */
				if (!access(path, R_OK | (S_ISDIR(st.st_mode) ? X_OK : 0))) value |= RIGHTS_READ;
				if (writable && !access(path, W_OK)) value |= RIGHTS_WRITE;
				attributes += pack(attributes, "c", (uint8_t)value);
				break;

			case ATTR_SIZE:
				bigvalue = st.st_size;
				attributes += pack(attributes, "l", bigvalue);
				break;

			case ATTR_DEV_ID:
				value = st.st_dev;
				attributes += pack(attributes, "i", (uint32_t)value);
				break;

			case ATTR_LINKS:
				value = st.st_nlink;
				attributes += pack(attributes, "i", (uint32_t)value);
				break;

			case ATTR_ATIME:
				bigvalue = newtp_timespec_to_time(st.st_atim);
				attributes += pack(attributes, "l", bigvalue);
				break;

			case ATTR_MTIME:
				bigvalue = newtp_timespec_to_time(st.st_mtim);
				attributes += pack(attributes, "l", bigvalue);
				break;

			case ATTR_CTIME:
				bigvalue = newtp_timespec_to_time(st.st_ctim);
				attributes += pack(attributes, "l", bigvalue);
				break;

			case ATTR_PTYPE:
				value = (st.st_mode & S_IFMT) >> 12;
				attributes += pack(attributes, "c", (uint8_t)value);
				break;

			case ATTR_PERMS:
				value = st.st_mode & (uint32_t)0x00000fff;
				attributes += pack(attributes, "s", (uint16_t)value);
				break;

			case ATTR_UID:
				value = st.st_uid;
				attributes += pack(attributes, "i", (uint32_t)value);
				break;

			case ATTR_GID:
				value = st.st_gid;
				attributes += pack(attributes, "i", (uint32_t)value);
				break;

			default:
				/* attribute was checked in as valid, but we don't know it.
				 * this is an internal error. TODO report it. */
				return -1;
		}
	}
	return ret;
}

/**** actual command implementations ****/

int cmd_ASSIGN (struct command * cmd, char * payload, char * response)
{
	logp("CMD_ASSIGN -> %d", cmd->handle);
	int res = handle_assign(cmd->handle, payload, cmd->length);
	return REPLY(res, 0);
}

int list_shares (struct command * cmd, char * response, char const * attr_spec, int attr_len)
{
	int filled = sizeof(uint16_t);
	char * buf = response + SIZEOF_reply();
	char * attrs;
	int entries = 0;
	int result = STAT_FINISHED;
	struct dir_entry entry;
	struct share * share = NULL;
	log("listing root");

	/* `buf` is starting at position after the reply packet,
	 * `filled` is size of payload in use, starts at length of entries counter */

	attrs = xmalloc(attr_len);
	entry.attr = attrs;
	entry.attr_len = attr_len;

	while ((share = share_next(share))) {
		entry.name_len = share->nlen;
		entry.name = share->name;

		if (fill_stat(share->path, share->writable, attrs, attr_spec, cmd->length) == -1) {
			if (errno == EACCES) {
				result = ERR_DENIED;
				break;
			} else if (errno == ENAMETOOLONG) {
				continue; /* ..... what else */
			} else if (errno == ELOOP || errno == ENOENT || errno == ENOTDIR) {
				/* should not happen but what do we know */
				result = ERR_NOTFOUND;
				break;
			} else {
				/* the list of remaining error codes in man stat(2) is
				 * pretty ominous: EFAULT, ENOMEM, EOVERFLOW */
				result = ERR_SERVFAIL;
				break;
			}
		}

		pack_dir_entry(buf + filled, &entry);
		if (filled + SIZEOF_dir_entry(&entry) > MAX_LENGTH) {
			break;
		}
		filled += SIZEOF_dir_entry(&entry);
		++entries;
	}

	free(attrs);

	/* write number of entries at start of buf */
	pack(buf, "s", (uint16_t)entries);
	return REPLY(result, filled);
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

	h->entry_name = NULL;
	h->entry_len = 0;

	return REPLY(STAT_OK, 0);
}

int cmd_READDIR (struct command * cmd, char * payload, char * response)
{
	int entries = 0, res = 0;
	int attr_len = 0;
	char * pathbuf;
	char * nameptr;
	char * attrs;

	int filled = sizeof(uint16_t);
	int result = STAT_FINISHED;
	char * buf = response + SIZEOF_reply();

	/* `buf` is starting at position after the reply packet,
	 * `filled` is size of payload in use, starts at length of entries counter */

	struct dir_entry entry;
	struct dirent * dirent;
	struct handle * h;

	VALIDATE_HANDLE(h);
	logp("CMD_READDIR %d (%s)", cmd->handle, h->path);

	attr_len = calculate_attr_len(payload, cmd->length);
	if (attr_len < 0) {
		log("invalid attribute string");
		return REPLY(ERR_BADATTR, 0);
	}

	if (!h->path[0]) return list_shares(cmd, response, payload, attr_len);

	if (!h->dir) {
		log("invalid continuation");
		return REPLY(ERR_READDIR, 0);
	}

	/* allocate sufficient attr length */
	attrs = xmalloc(attr_len);
	entry.attr = attrs;
	entry.attr_len = attr_len;
	/* prepare path buffer */
	pathbuf = xmalloc(LONGEST_PATH);
	strncpyz(pathbuf, h->path, h->plen);
	pathbuf[h->plen] = '/';
	nameptr = pathbuf + h->plen + 1;

	if (h->entry_name) { /* append carried-over entry */
		strncpyz(nameptr, h->entry_name, h->entry_len);
		res = fill_stat(pathbuf, h->writable, attrs, payload, cmd->length);
		entry.name_len = h->entry_len;
		entry.name = nameptr;
		pack_dir_entry(buf + filled, &entry);
		filled += SIZEOF_dir_entry(&entry);
		++entries;

		h->entry_name = NULL;
		h->entry_len = 0;
	}

	errno = 0;
	while ((dirent = readdir(h->dir))) {
		/* skip "." and ".." */
		if (dirent->d_name[0] == '.')
			if (dirent->d_name[1] == 0 || (dirent->d_name[1] == '.' && dirent->d_name[2] == 0)) continue;

		/* fill the entry */
		assert(h->plen + 1 + NAME_MAX + 1 < LONGEST_PATH);
		entry.name_len = strlen(dirent->d_name);
		
		if (filled + SIZEOF_dir_entry(&entry) > MAX_LENGTH) {
			/* save entry name for resume */
			h->entry_name = dirent->d_name; /* this is not memory-managed by us */
			h->entry_len = entry.name_len;
			result = STAT_CONTINUED;
			break;
		}

		/* proceed with entry */
		strncpyz(nameptr, dirent->d_name, entry.name_len);
		res = fill_stat(pathbuf, h->writable, attrs, payload, cmd->length);
		if (res == -1) {
			if (errno == EACCES) {
				result = ERR_DENIED;
				break;
			} else if (errno == ENAMETOOLONG) {
				continue; /* ..... what else */
			} else if (errno == ELOOP) {
				result = ERR_NOTFOUND; /* best possible solution here? */
				break;
			} else if (errno == ENOENT || errno == ENOTDIR) {
				/* should not happen but what do we know */
				result = ERR_NOTFOUND;
				break;
			} else {
				/* the list of remaining error codes in man stat(2) is
				 * pretty ominous: EFAULT, ENOMEM, EOVERFLOW */
				result = ERR_SERVFAIL;
				break;
			}
		}
		entry.name = nameptr;
		pack_dir_entry(buf + filled, &entry);
		filled += SIZEOF_dir_entry(&entry);
		++entries;

		errno = 0;
	}

	free(pathbuf);
	free(attrs);

	if (!dirent) {
		/* close directory */
		closedir(h->dir);
		h->dir = 0;
		h->entry_len = 0;
		h->entry_name = NULL;

		/* check for errors */
		if (errno == EBADF) {
			result = ERR_READDIR;
		} else if (errno) {
			result = ERR_FAIL;
		}
	}

	logp("sent %d items, %d bytes", entries, filled);
	pack(buf, "s", (uint16_t)entries);
	return REPLY(result, filled);
}

int cmd_STAT (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	int attr_len;

	VALIDATE_HANDLE(h);
	logp("CMD_STAT %d (%s)",  cmd->handle, h->path);

	attr_len = calculate_attr_len(payload, cmd->length);
	if (attr_len < 0) {
		log("invalid attribute string");
		return REPLY(ERR_BADATTR, 0);
	}

	if (fill_stat(h->path, h->writable, response + SIZEOF_reply(), payload, cmd->length) == -1) {
		switch (errno) {
			case EACCES:       return REPLY(ERR_DENIED, 0);
			case ELOOP:        return REPLY(ERR_NOTFOUND, 0);
			case ENAMETOOLONG: return REPLY(ERR_BADPATH, 0); /* name too long heh heh */
			case ENOENT:       return REPLY(ERR_NOTFOUND, 0);
			case ENOTDIR:      return REPLY(ERR_NOTFOUND, 0);
			case EOVERFLOW:    return REPLY(ERR_SERVFAIL, 0);
			default:           return REPLY(ERR_FAIL, 0);
		}
	}

	return REPLY(STAT_OK, attr_len);
}

int cmd_SETATTR (struct command * cmd, char * payload, char * response)
{
/*	struct handle * h;
	uint8_t attr;
	uint8_t byte;
	uint16_t uint16;
	uint32_t uint32;
	uint64_t uint64;
	VALIDATE_HANDLE(h);


	logp("CMD_SETATTR %d (%s): attr 0x%02x",  cmd->handle, h->path);

	attr_len = calculate_attr_len(payload, cmd->length);
	if (attr_len < 0) {
		log("invalid attribute string");
		return REPLY(ERR_BADATTR, 0);
	}

	if (fill_stat(h->path, h->writable, response + SIZEOF_reply(), payload, cmd->length) == -1) {
		switch (errno) {
			case EACCES:       return REPLY(ERR_DENIED, 0);
			case ELOOP:        return REPLY(ERR_NOTFOUND, 0);
			case ENAMETOOLONG: return REPLY(ERR_BADPATH, 0);
			case ENOENT:       return REPLY(ERR_NOTFOUND, 0);
			case ENOTDIR:      return REPLY(ERR_NOTFOUND, 0);
			case EOVERFLOW:    return REPLY(ERR_SERVFAIL, 0);
			default:           return REPLY(ERR_FAIL, 0);
		}
	}

	return REPLY(STAT_OK, attr_len);*/ return -1;
}

int cmd_READ (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	int err;
	int done = 0;
	char * buf = response + SIZEOF_reply();
	struct params_offlen params;

	DIE_OR(unpack_params_offlen(payload, cmd->length, &params));

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
			else if (errno == ENOTDIR) err = ERR_NOTFOUND;
			else if (errno == EISDIR) err = ERR_NOTFILE;
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
	uint16_t total;
	uint64_t offset;

	/* be a good guy and pick parameters first */
	DIE_OR(unpack(payload, cmd->length, "l", &offset));
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
		RETRY1(h->fd, open(h->path, O_CREAT | O_WRONLY, 0666)); /* default noexec mode, modulo umask */
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
			else if (errno == ENOSPC || errno == EDQUOT) err = ERR_DEVFULL;
			else if (errno == EFBIG) err = ERR_TOOBIG;
			else err = ERR_FAIL;
			return REPLY(err, sizeof(uint16_t));
		} else {
			payload += err;
			write_length -= err;
			total += err;
		}
	}
	/* we have received and written all the requested data */
	pack(response + SIZEOF_reply(), "s", total);
	return REPLY(STAT_OK, sizeof(uint16_t));
}

int cmd_TRUNCATE (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	uint64_t offset;
	int res, err = STAT_OK;

	DIE_OR(unpack(payload, cmd->length, "l", &offset));
	VALIDATE_HANDLE(h);
	logp("CMD_TRUNCATE %d (%s): ofs %llu", cmd->handle, h->path, (long long unsigned)offset);
	if (!h->writable) return REPLY(ERR_DENIED, sizeof(uint16_t));

	RETRY0(res, truncate(h->path, offset));
	if (res == -1) {
		if (errno == EACCES) err = ERR_DENIED;
		else if (errno == EISDIR) err = ERR_NOTFILE;
		else if (errno == ENOENT) err = ERR_NOTFOUND;
		else if (errno == ENOTDIR) err = ERR_NOTFOUND;
		else if (errno == EFBIG) err = ERR_TOOBIG;
		else if (errno == EINVAL) err = ERR_TOOBIG;
		else if (errno == EIO) err = ERR_IO;
		else if (errno == ELOOP) err = ERR_BADPATH;
		else if (errno == ENAMETOOLONG) err = ERR_BADPATH;
		else if (errno == EPERM) err = ERR_UNSUPPORTED;
		else if (errno == EROFS) err = ERR_DENIED;
		else err = ERR_FAIL;
	}
	return REPLY(err, 0);
}

int cmd_DELETE (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	int res, err = STAT_OK;
	VALIDATE_HANDLE(h);
	logp("CMD_DELETE %d (%s)", cmd->handle, h->path);
	if (!h->writable) return REPLY(ERR_DENIED, 0);
	res = remove(h->path);
	if (res == -1) {
		if (errno == EACCES) err = ERR_DENIED;
		else if (errno == EBUSY) err = ERR_BUSY;
		else if (errno == EIO) err = ERR_IO;
		else if (errno == EISDIR || errno == EPERM)
			/* this really should not be happening with remove(3) */
			err = ERR_FAIL;
		else if (errno == ELOOP) err = ERR_BADPATH;
		else if (errno == ENAMETOOLONG) err = ERR_BADPATH;
		else if (errno == ENOENT) err = ERR_NOTFOUND;
		else if (errno == ENOTDIR) err = ERR_NOTFOUND;
		else if (errno == EINVAL) err = ERR_BADPATH;
		else if (errno == EROFS) err = ERR_DENIED;
		else if (errno == ENOTEMPTY) err = ERR_NOTEMPTY;
		else err = ERR_FAIL;
	}
	return REPLY(err, 0);
}

int cmd_RENAME (struct command * cmd, char * payload, char * response)
{
	struct handle * h, * nh;
	int res, err = STAT_OK;
	VALIDATE_HANDLE(h);
	logp("CMD_RENAME %d (%s)", cmd->handle, h->path);
	if (!h->writable) return REPLY(ERR_DENIED, 0);

	nh = handle_make(payload, cmd->length);
	if (!nh) return ERR_BADPATH;
	else if (!nh->path) {
		/* TODO too intimate knowledge of handle internals */
		free(nh->name);
		free(nh);
		return ERR_NOTFOUND;
		/* TODO ERR_DENIED for moving to root */
	}

	res = rename(h->path, nh->path);
	if (res == -1) {
		if (errno == EACCES || errno == EPERM) err = ERR_DENIED;
		else if (errno == EBUSY) err = ERR_BUSY;
		else if (errno == EDQUOT) err = ERR_DENIED;
		else if (errno == EINVAL) err = ERR_BADMOVE;
		else if (errno == EISDIR) err = ERR_BADMOVE;
		else if (errno == ELOOP) err = ERR_BADPATH;
		else if (errno == ENAMETOOLONG) err = ERR_BADPATH;
		else if (errno == ENOENT) err = ERR_BADMOVE;
		else if (errno == ENOTDIR) err = ERR_BADMOVE;
		else if (errno == ENOTEMPTY || errno == EEXIST) err = ERR_BADMOVE;
		else if (errno == EIO) err = ERR_IO;
		else if (errno == EROFS) err = ERR_DENIED;
		else if (errno == EXDEV) err = ERR_CROSSDEV;
		else err = ERR_FAIL;
	} else {
		handle_assign_ptr(cmd->handle, nh);
	}
	return REPLY(err, 0);
}

int cmd_MAKEDIR (struct command * cmd, char * payload, char * response)
{
	struct handle * h;
	int res, err = STAT_OK;
	VALIDATE_HANDLE(h);
	logp("CMD_MAKEDIR %d (%s)", cmd->handle, h->path);
	if (!h->writable) return REPLY(ERR_DENIED, 0);
	res = mkdir(h->path, 0777);
	if (res == -1) {
		if (errno == EACCES || errno == EPERM || errno == EROFS) err = ERR_DENIED;
		else if (errno == EEXIST) err = ERR_EXISTS;
		else if (errno == ELOOP) err = ERR_NOTFOUND;
		else if (errno == ENAMETOOLONG) err = ERR_BADPATH;
		else if (errno == ENOENT) err = ERR_NOTFOUND;
		else if (errno == ENOSPC || errno == EDQUOT) err = ERR_DEVFULL;
		else if (errno == ENOTDIR) err = ERR_NOTFOUND;
		else if (errno == EBUSY) err = ERR_BUSY;
		else if (errno == EIO) err = ERR_IO;
		else err = ERR_FAIL;
	}
	return REPLY(err, 0);
}
