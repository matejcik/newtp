#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clientops.h"
#include "commands.h"
#include "common.h"
#include "log.h"
#include "structs.h"
#include "tools.h"

#define FUSE_USE_VERSION 28
#include <fuse.h>

#define STAT_ATTR_QUERY "\x10\x11\x04\x13\x14\x02\x05\x06\x12"
#define STAT_RESULT_LENGTH (1 + 2 + 4 + 4 + 4 + 8 + 8 + 8 + 8)


#define MAX_HANDLES 16384
#define HASH_MODULE 4679

typedef struct {
	int size;
	int items;
	uint16_t * bucket;
} hash_bucket;

struct connection_info {
	struct intro intro;

	char ** handles;
	int * handles_open;
	uint16_t max_handles;
	uint16_t cur_handle;
	hash_bucket * handle_ht;

	int opendirs;
};

static struct connection_info conn;
static struct fuse_operations newtp_oper;

int main(int argc, char** argv) {
	int ret;
	memset(&conn, 0, sizeof(conn));

	/* connect */
	if (newtp_client_connect("localhost", "63987", &conn.intro, NULL)) return 1;

	/* initialize conn */
	conn.max_handles = (MAX_HANDLES < conn.intro.max_handles) ? MAX_HANDLES : conn.intro.max_handles;
	conn.handles = xmalloc(sizeof(char *) * conn.max_handles);
	conn.handles_open = xmalloc(sizeof(int) * conn.max_handles);
	conn.handle_ht = xmalloc(sizeof(hash_bucket) * HASH_MODULE);

	/* proceed */
	ret = fuse_main(argc, argv, &newtp_oper, NULL);

	newtp_gnutls_disconnect(1);
	return ret;
}

/* the djb2 hashing function
 * http://www.cse.yorku.ca/~oz/hash.html */
unsigned long djb2 (char const * str)
{
	unsigned char * s = (unsigned char *)str;
	unsigned long hash = 5381;
	int c;
	while ((c = *s++))
		hash = ((hash << 5) + hash) ^ c; /* hash * 33 ^ c */
	return hash;
}


void replace_handle (uint16_t slot, char const * newpath, int len)
{
	unsigned long hash = djb2(newpath) % HASH_MODULE;
	hash_bucket * bucket;

	/* is the slot occupied? */
	if (conn.handles[slot]) {
		/* destroy it */
		conn.handles_open[slot] = 0;
		unsigned long h2 = djb2(conn.handles[slot]) % HASH_MODULE;
		free(conn.handles[slot]);
		/* delete hashtable entry */
		bucket = conn.handle_ht + h2;
		int p;
		for (p = 0; (p < bucket->items) && (bucket->bucket[p] != slot); p++) { /* nothing */ }
		for (; p < bucket->size - 1; p++)
			bucket->bucket[p] = bucket->bucket[p + 1];
		bucket->items--;
	}

	/* install new entry */
	conn.handles[slot] = xmalloc(len + 1);
	strcpy(conn.handles[slot], newpath);
	/* place into hashtable */
	bucket = conn.handle_ht + hash;
	if (bucket->items == bucket->size) {
		if (bucket->size == 0) bucket->size = 32;
		else bucket->size *= 2;
		bucket->bucket = xrealloc(bucket->bucket, sizeof(uint16_t) * bucket->size);
	}
	bucket->bucket[bucket->items++] = slot;
}

/* gets handle from hashtable or assigns a new one */
uint16_t get_handle (char const * path) {
	struct reply reply;
	unsigned long hash = djb2(path) % HASH_MODULE;
	int len = strlen(path);
	hash_bucket * bucket;

	/* perform hash lookup */
	bucket = conn.handle_ht + hash;
	for (int i = 0; i < bucket->items; i++) {
		uint16_t handle = bucket->bucket[i];
		if (strcmp(conn.handles[handle], path)) continue;
		/* found it */
		return handle;
	}
	/* path is not in table */

	/* can we allocate more? */
	int ch = conn.cur_handle;
	/* find first non-open handle. that should not take long
	 * if the slot is free, it will not be open */
	while (conn.handles_open[ch]) ch = (ch + 1) % conn.max_handles;
	replace_handle(ch, path, len);

	conn.cur_handle = (ch + 1) % conn.max_handles;

	/* assign on server */
	strcpy(data_out, path);
	reply_for_command(0, CMD_ASSIGN, ch, len, &reply);
	assert(reply.result == STAT_OK);

	return ch;
}

/**** value converters ****/

void newtp_attr_to_stat (struct stat * st, char * attrs)
{
	uint64_t size, ctime, mtime, atime;
	uint32_t uid, gid, links;
	uint16_t perms;
	uint8_t  type;

	unpack(attrs, STAT_RESULT_LENGTH, "csiiillll", &type, &perms, &links,
		&uid, &gid, &size, &atime, &mtime, &ctime);

	memset(st, 0, sizeof(struct stat));
	st->st_mode = (type << 12) | perms;
	st->st_nlink = links;
	st->st_uid = uid;
	st->st_gid = gid;
	st->st_size = size;
	newtp_time_to_timespec(&st->st_atim, atime);
	newtp_time_to_timespec(&st->st_mtim, mtime);
	newtp_time_to_timespec(&st->st_ctim, ctime);
}

int newtp_result_to_errno (int result)
{
	if (result < 0x80) return 0;
	switch(result) {
		case ERR_DENIED:   return -EACCES;
		case ERR_BUSY:     return -EBUSY;
		case ERR_BADPATH:
		case ERR_NOTFOUND: return -ENOENT;
		case ERR_NOTDIR:   return -ENOTDIR;
		case ERR_NOTFILE:  return -ENFILE;

		case ERR_BADOFFSET:return -EINVAL;
		case ERR_TOOBIG:   return -EFBIG;
		case ERR_DEVFULL:  return -ENOSPC;
		case ERR_NOTEMPTY: return -ENOTEMPTY;
		default:           return -EIO;
	}
}
#define MAYBE_RET {\
	int _r = newtp_result_to_errno(reply.result); \
	if (_r < 0) return _r; \
}

/**** filesystem calls ****/

int newtp_getattr (char const * path, struct stat * st)
{
	struct reply reply;
	int handle;

	memset(st, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0755;
		return 0;
	}

	handle = get_handle(path);
	strcpy(data_out, STAT_ATTR_QUERY);
	reply_for_command(0, CMD_STAT, handle, strlen(STAT_ATTR_QUERY), &reply);
	MAYBE_RET;

	if (reply.length < STAT_RESULT_LENGTH) return -EIO;

	newtp_attr_to_stat(st, data_in);

	return 0;
}

int newtp_opendir (char const * path, struct fuse_file_info * fi)
{
	struct reply reply;
	/* "/" is forbidden in NewTP */
	if (strcmp(path, "/") == 0) path = "";

	int handle = get_handle(path);
	if (conn.opendirs > conn.intro.max_opendirs) /* reached max number of open dirs */
		return -ENFILE;

	/* rewind */
	reply_for_command(0, CMD_REWINDDIR, handle, 0, &reply);
	MAYBE_RET;
	/* success */
	conn.opendirs++;
	conn.handles_open[handle] = 1;
	fi->fh = handle;
	return 0;
}

int newtp_releasedir (char const * path, struct fuse_file_info * fi)
{
	int handle = fi->fh;
	conn.handles_open[handle] = 0;
	conn.opendirs--;
	return 0;
}

int newtp_readdir (char const * path, void * buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info * fi)
{
	struct reply reply;
	struct dir_entry entry;
	struct stat st;
	int handle = fi->fh;
	int remaining;
	uint16_t items;
	char * item;

	memset(&st,    0, sizeof(struct stat));
	memset(&entry, 0, sizeof(struct dir_entry));

	st.st_mode = S_IFDIR | 0755;
	filler(buf, "." , &st, 0);
	filler(buf, "..", &st, 0);

	strcpy(data_out, STAT_ATTR_QUERY);
	do {
		reply_for_command(0, CMD_READDIR, handle, strlen(STAT_ATTR_QUERY), &reply);
		if (reply.result >= 0x80 || reply.length < 2) return -EIO;
		unpack(data_in, 2, "s", &items);
		remaining = reply.length - 2;
		item = data_in + 2;
		for (int i = 0; i < items; i++) {
			int len = unpack_dir_entry(item, remaining, &entry);
			if (len < 0 || entry.attr_len < STAT_RESULT_LENGTH) {
				free(entry.name); free(entry.attr);
				err("malformed packet in directory listing");
				return -EIO;
			}
			newtp_attr_to_stat(&st, entry.attr);
			filler(buf, entry.name, &st, 0);
			free(entry.name); free(entry.attr);
			entry.name = entry.attr = NULL;
			item += len; remaining -= len;
		}
	} while (reply.result != STAT_FINISHED);

	return 0;
}

int newtp_read (char const * path, char * buf, size_t size, off_t offset,
		struct fuse_file_info * fi)
{
	struct reply reply;
	struct params_offlen params;
	int handle = fi->fh;
	size_t total = 0;
	
	params.offset = offset;
	while (total < size) {
		if (size - total > MAX_LENGTH) params.length = MAX_LENGTH;
		else params.length = size - total;
		pack_params_offlen(data_out, &params);
		reply_for_command(0, CMD_READ, handle, SIZEOF_params_offlen(), &reply);
		MAYBE_RET;
		memcpy(buf + total, data_in, reply.length);
		total += reply.length;
		params.offset += reply.length;
		if (reply.length < params.length) return total;
	}
	return total;
}

int newtp_write (char const * path, char const * buf, size_t size, off_t offset,
		struct fuse_file_info * fi)
{
	struct reply reply;
	int handle = fi->fh;
	size_t total = 0;
	uint64_t off = offset;
	uint16_t len = 0, retlen = 0;

	while (total < size) {
		if (size - total > MAX_LENGTH - 8) len = MAX_LENGTH - 8;
		else len = size - total;
		pack(data_out, "l", off);
		memcpy(data_out + 8, buf + total, len);
		reply_for_command(0, CMD_WRITE, handle, len + 8, &reply);
		MAYBE_RET;
		assert(reply.length >= 2);
		unpack(data_in, reply.length, "s", &retlen);
		total += retlen;
		off += retlen;
		if (retlen < len) return total;
	}
	return total;
}

int newtp_open (char const * path, struct fuse_file_info * fi)
{
	int handle = get_handle(path);
	/* mark the handle as open */
	conn.handles_open[handle] = 1;
	/* FUSE stats the file and checks for permissions,
	 * if there's a race, we can't do anything anyway */
	fi->fh = handle;
	return 0;
}

int newtp_release (char const * path, struct fuse_file_info * fi)
{
	int handle = fi->fh;
	conn.handles_open[handle] = 0;
	return 0;
}

int newtp_create (char const * path, mode_t mode, struct fuse_file_info * fi)
{
	struct reply reply;
	int handle = get_handle(path);
	(void) mode; /* we don't use mode */
	pack(data_out, "l", (uint64_t)0);
	reply_for_command(0, CMD_WRITE, handle, 8, &reply);
	MAYBE_RET;
	/* created successfully, that's as much as we can hope for */
	conn.handles_open[handle] = 1;
	fi->fh = handle;
	return 0;
}

int newtp_truncate (char const * path, off_t offset)
{
	struct reply reply;
	int handle = get_handle(path);
	uint64_t off = offset;

	pack(data_out, "l", off);
	reply_for_command(0, CMD_TRUNCATE, handle, 8, &reply);
	return newtp_result_to_errno(reply.result);
}

int newtp_unlink (char const * path)
{
	struct reply reply;
	int handle = get_handle(path);
	reply_for_command(0, CMD_DELETE, handle, 0, &reply);
	return newtp_result_to_errno(reply.result);
}

int newtp_mkdir (char const * path, mode_t mode)
{
	struct reply reply;
	int handle = get_handle(path);
	(void) mode; /* modes not supported, as we well know */
	reply_for_command(0, CMD_MAKEDIR, handle, 0, &reply);
	return newtp_result_to_errno(reply.result);
}

int newtp_rename (char const * path, char const * newpath)
{
	struct reply reply;
	int handle = get_handle(path);
	int len = strlen(newpath);
	strcpy(data_out, newpath);
	reply_for_command(0, CMD_RENAME, handle, len, &reply);
	MAYBE_RET;
	replace_handle(handle, newpath, len);
	return 0;
}

int newtp_utimens (char const * path, struct timespec const ts[2])
{
	struct reply reply;
	int handle = get_handle(path);
	uint64_t atime = newtp_timespec_to_time(ts[0]);
	uint64_t mtime = newtp_timespec_to_time(ts[1]);
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	if (ts[0].tv_nsec == UTIME_NOW) atime = newtp_timespec_to_time(now);
	if (ts[1].tv_nsec == UTIME_NOW) mtime = newtp_timespec_to_time(now);

	if (ts[0].tv_nsec != UTIME_OMIT) {
		pack(data_out, "cl", (uint8_t)ATTR_ATIME, atime);
		reply_for_command(0, CMD_SETATTR, handle, 9, &reply);
		MAYBE_RET;
	}
	if (ts[1].tv_nsec != UTIME_OMIT) {
		pack(data_out, "cl", (uint8_t)ATTR_MTIME, mtime);
		reply_for_command(0, CMD_SETATTR, handle, 9, &reply);
		MAYBE_RET;
	}

	return 0;
}

int newtp_chown (char const * path, uid_t uid, gid_t gid)
{
	struct reply reply;
	int handle = get_handle(path);
	if (uid > -1) {
		pack(data_out, "ci", (uint8_t)ATTR_UID, (uint32_t)uid);
		reply_for_command(0, CMD_SETATTR, handle, 5, &reply);
		MAYBE_RET;
	}
	if (gid > -1) {
		pack(data_out, "ci", (uint8_t)ATTR_GID, (uint32_t)gid);
		reply_for_command(0, CMD_SETATTR, handle, 5, &reply);
		MAYBE_RET;
	}
	return 0;
}

int newtp_chmod (char const * path, mode_t mode)
{
	struct reply reply;
	int handle = get_handle(path);
	pack(data_out, "cs", (uint8_t)ATTR_PERMS, (uint16_t)(mode & 0x0fff));
	reply_for_command(0, CMD_SETATTR, handle, 3, &reply);
	return newtp_result_to_errno(reply.result);
}


static struct fuse_operations newtp_oper = {
	.getattr    = newtp_getattr,
	.opendir    = newtp_opendir,
	.readdir    = newtp_readdir,
	.releasedir = newtp_releasedir,
	.create     = newtp_create,
	.open       = newtp_open,
	.read       = newtp_read,
	.write      = newtp_write,
	.release    = newtp_release,
	.truncate   = newtp_truncate,
	.unlink     = newtp_unlink,
	.rmdir      = newtp_unlink,
	.mkdir      = newtp_mkdir,
	.rename     = newtp_rename,
	.utimens    = newtp_utimens,
	.chmod      = newtp_chmod,
	.chown      = newtp_chown,
};

/*
static struct fuse_operations newtp_oper = {
	.getattr    = newtp_getattr,
	.fgetattr   = newtp_fgetattr,
	.access     = newtp_access,
	.readlink   = newtp_readlink,
	.opendir    = newtp_opendir,
	.readdir    = newtp_readdir,
	.releasedir = newtp_releasedir,
	.mkdir      = newtp_mkdir,
	.symlink    = newtp_symlink,
	.unlink     = newtp_unlink,
	.rmdir      = newtp_rmdir,
	.rename     = newtp_rename,
	.link       = newtp_link,
	.chmod      = newtp_chmod,
	.chown      = newtp_chown,
	.truncate   = newtp_truncate,
	.ftruncate  = newtp_ftruncate,
	.utimens    = newtp_utimens,
	.create     = newtp_create,
	.open       = newtp_open,
	.read       = newtp_read,
	.write      = newtp_write,
	.statfs     = newtp_statfs,
	.flush      = newtp_flush,
	.release    = newtp_release,
	.fsync      = newtp_fsync,
};
*/
