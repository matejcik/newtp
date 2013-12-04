#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "clientops.h"
#include "commands.h"
#include "common.h"
#include "log.h"
#include "structs.h"

#define FUSE_USE_VERSION 28
#include <fuse.h>

#define STAT_ATTR_QUERY "\x10\x11\x04\x13\x14\x02\x05\x06\x12"
#define STAT_RESULT_LENGTH (1 + 2 + 4 + 4 + 4 + 8 + 8 + 8 + 8)

uint16_t get_handle (char const * path) {
	struct reply reply;

	/* TODO path length */
	strncpy(data_out, path, MAX_LENGTH);
	reply_for_command(0, CMD_ASSIGN, 1, strlen(path), &reply);
	assert(reply.result == STAT_OK);

	return 1;
}

void newtp_time_to_timespec (struct timespec *ts, uint64_t ntime)
{
	/* ntime is in microseconds */
	long long sec  = ntime / 1000000;
	long long nsec = (ntime - (sec * 1000000)) * 1000;
	assert(nsec < LONG_MAX);
	ts->tv_sec  = sec;
	ts->tv_nsec = nsec;
}

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
		case ERR_BADPATH:
		case ERR_NOTFOUND: return -ENOENT;
		case ERR_NOTDIR:   return -ENOTDIR;
		case ERR_NOTFILE:  return -ENFILE;

		case ERR_BADOFFSET:return -EINVAL;
		case ERR_TOOBIG:   return -EFBIG;
		case ERR_DEVFULL:  return -ENOSPC;
		default:           return -EIO;
	}
}
#define MAYBE_RET {\
	int _r = newtp_result_to_errno(reply.result); \
	if (_r < 0) return _r; \
}

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

int newtp_readdir (char const * path, void * buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info * fi)
{
	struct reply reply;
	struct dir_entry entry;
	struct stat st;
	int handle, remaining;
	uint16_t items;
	char * item;

	memset(&st,    0, sizeof(struct stat));
	memset(&entry, 0, sizeof(struct dir_entry));
	if (strcmp(path, "/") == 0) path = "";

	handle = get_handle(path);
	reply_for_command(0, CMD_REWINDDIR, handle, 0, &reply);
	MAYBE_RET;

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
		struct fuse_file_info *fi)
{
	struct reply reply;
	struct params_offlen params;
	int handle = get_handle(path);
	size_t total = 0;
	
	params.offset = offset;
	while (size > 0) {
		if (size > MAX_LENGTH) params.length = MAX_LENGTH;
		else params.length = size;
		pack_params_offlen(data_out, &params);
		reply_for_command(0, CMD_READ, handle, SIZEOF_params_offlen(), &reply);
		MAYBE_RET;
		memcpy(buf, data_in, reply.length);
		size -= reply.length;
		total += reply.length;
		params.offset += reply.length;
		if (reply.length < params.length) return total;
	}
	return total;
}

int newtp_write (char const * path, char const * buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	struct reply reply;
	int handle = get_handle(path);
	size_t total = 0;
	uint64_t off = offset;
	uint16_t len = 0, retlen = 0;

	while (size > 0) {
		if (size > MAX_LENGTH - 8) len = MAX_LENGTH - 8;
		else len = size;
		pack(data_out, "l", off);
		memcpy(data_out + 8, buf, len);
		reply_for_command(0, CMD_WRITE, handle, len + 8, &reply);
		MAYBE_RET;
		assert(reply.length >= 2);
		unpack(data_in, reply.length, "s", &retlen);
		size -= retlen;
		total += retlen;
		off += retlen;
		if (retlen < len) return total;
	}
	return total;
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



static struct fuse_operations newtp_oper = {
	.getattr  = newtp_getattr,
	.readdir  = newtp_readdir,
	.read     = newtp_read,
	.write    = newtp_write,
	.truncate = newtp_truncate,
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

int main(int argc, char** argv) {
	struct intro intro;
	int ret;
	if (newtp_client_connect("localhost", "63987", &intro, NULL)) return 1;
	ret = fuse_main(argc, argv, &newtp_oper, NULL);
	newtp_gnutls_disconnect(1);
	return ret;
}
