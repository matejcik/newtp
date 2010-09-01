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
#include "structs.h"
#include "common.h"
#include "log.h"
#include "tools.h"

extern int sock_client;

void die ()
{
	log("exiting");
	close(sock_client);
	exit(1);
}

static void at_exit ()
{
	log("closed client connection");
	close(sock_client);
}

struct intro whoami;

struct handle {
	char *name;
	FILE *file;
	DIR *dir;

	struct dir_entry entry;
	int elen;
};

#define MAXHANDLES 16
struct handle * handles[MAXHANDLES];

struct handle * newhandle(char* name)
{
	struct handle * h = xmalloc(sizeof(struct handle));
	h->name = name;
	h->file = NULL;
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
#define DOTS 2
#define OTHER 0
	int state = OTHER;
	char *c = pkt->data;
	char *name;
	int pos = 0;

	if (handle >= MAXHANDLES) return STAT_BADHANDLE;

	if (*c != '/') return STAT_BADPATH;
	while (pos < pkt->len) {
		if (!*c) return STAT_BADPATH;
		if (state == DOTS && *c == '/') return STAT_BADPATH;
		if (state == SLASH && *c == '/') return STAT_BADPATH;
		if (*c == '/') state = SLASH;
		else if ((state == SLASH || state == DOTS) && *c == '.') state = DOTS;
		else state = OTHER;

		++pos; ++c;
	}

	if (handles[handle]) delhandle(handles[handle]);
	name = xmalloc(pkt->len + 2);
	name[0] = '.';
	name[pkt->len + 1] = 0;
	strncpy(name + 1, pkt->data, pkt->len);
	handles[handle] = newhandle(name);
	return STAT_OK;
}

#define VALIDATE_HANDLE(cmd) { \
	if (cmd.handle >= MAXHANDLES || handles[cmd.handle] == NULL) { \
		send_reply_p(sock_client, cmd.id, STAT_BADHANDLE); \
		break; \
	} \
}

int fill_entry (char const * directory, int dlen, struct dirent const * dirent, struct dir_entry * entry)
{
	char * path;
	int len, ret;
	struct stat st;
	assert(entry); assert(dirent);
	/* entry is pre-allocated and cleared, dirent should already be checked for null */

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

		if ((st.st_uid == getuid() && st.st_mode & S_IRUSR)
		 || (st.st_gid == getgid() && st.st_mode & S_IRGRP)
		 || (st.st_mode && S_IROTH)) entry->perm |= PERM_READ;
		if ((st.st_uid == getuid() && st.st_mode & S_IWUSR)
		 || (st.st_gid == getgid() && st.st_mode & S_IWGRP)
		 || (st.st_mode && S_IWOTH)) entry->perm |= PERM_WRITE;
	}

	return SIZEOF_dir_entry - sizeof(char*) + entry->len;
}

int do_initdir (int id, struct handle * h)
{
	if (h->dir) closedir(h->dir);
	h->dir = opendir(h->name);
	if (!h->dir) {
		int err;
		if (errno == EACCES) err = STAT_EACCESS;
		else if (errno == ENOENT) err = STAT_NOTFOUND;
		else if (errno == ENOTDIR) err = STAT_NOTDIR;
		else if (errno == EMFILE || errno == ENFILE) /* TODO handle this better */ err = STAT_BADHANDLE;
		else err = STAT_SERVFAIL;

		send_reply_p(sock_client, id, err);
		return 0;
	}
	h->entry.len = 0;
	h->entry.name = NULL;
	log("init successful");
	return 1;
}

void do_listdir (int id, struct handle * h)
{
	char * buffer;
	int filled = 0;
	int dlen;
	struct dirent * dirent;

	dlen = strlen(h->name);

	/* continues listing on preinitialized dir handle */
	if (!h->dir) {
		send_reply_p(sock_client, id, STAT_NOCONTINUE);
		log("invalid continuation");
		return;
	}

#define BUFLEN 128000
	buffer = xmalloc(BUFLEN);
	while ((dirent = readdir(h->dir))) {
		h->elen = fill_entry(h->name, dlen, dirent, &h->entry);
		if (h->entry.type == ENTRY_BAD) continue;
		if (filled + h->elen < BUFLEN) {
			pack(buffer + filled, FORMAT_dir_entry,
				h->entry.len, h->entry.name,
				h->entry.type, h->entry.perm, h->entry.size);
			filled += h->elen;
		} else {
			send_reply_p(sock_client, id, STAT_CONT);
			send_data(sock_client, buffer, filled);
			free(buffer);
			logp("sent %d bytes continued", filled);
			return;
		}
	}
	/* we got to the end */
	send_reply_p(sock_client, id, STAT_OK);
	send_data(sock_client, buffer, filled);
	free(buffer);
	free(h->entry.name);
	closedir(h->dir);
	h->dir = NULL;
	logp("sent %d bytes", filled);
}

void work (void)
{
	struct command cmd;
	struct data_packet pkt;
	struct handle * h;
	int reply;

	log("connection received");
	atexit(&at_exit);

	// clear handles
	for (int i = 0; i < MAXHANDLES; i++) handles[i] = NULL;

	/* intro */
	whoami.version = 0;
	whoami.maxhandles = MAXHANDLES;
	whoami.handlelen = 4096;

	SAFE(send_intro(sock_client, &whoami));

	/* cmd loop */
	while (1) {
//		recv_full(&cmd, sizeof(cmd));
		SAFE(recv_command(sock_client, &cmd));

		switch (cmd.command) {
			case CMD_NOOP:
				log("ping");
				SAFE(send_reply_p(sock_client, cmd.id, STAT_OK));
				break;

			case CMD_ASSIGN:
				// read data
				SAFE(recv_data(sock_client, &pkt));

				reply = validate_assign_handle(cmd.handle, &pkt);
				if (reply == STAT_OK) {
					logp("assigning to handle %d, name '%s'", cmd.handle, handles[cmd.handle]->name);
				} else {
					logp("assigning to handle %d failed: %d", cmd.handle, reply);
				}
				SAFE(send_reply_p(sock_client, cmd.id, reply));
				break;

			case CMD_LIST:
				// list directory - ha ha!
				VALIDATE_HANDLE(cmd);
				h = handles[cmd.handle];
				logp("listing directory %s", h->name);
				if (do_initdir(cmd.id, h)) do_listdir(cmd.id, h);
				break;
			case CMD_LIST_CONT:
				VALIDATE_HANDLE(cmd);
				h = handles[cmd.handle];
				do_listdir(cmd.id, h);
				break;
		}
	}
}
