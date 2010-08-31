#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
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
		send_reply_p(sock_client, cmd.id, STAT_BADHANDLE, 0); \
		break; \
	} \
}

void work (void)
{
	struct command cmd;
	struct data_packet pkt;
	int reply;
	DIR * dirent = NULL;

	log("connection received");
	atexit(&at_exit);

	// clear handles
	for (int i = 0; i < MAXHANDLES; i++) handles[i] = NULL;

	/* intro */
	whoami.version = 0;
	whoami.maxhandles = MAXHANDLES;
	whoami.handlelen = 4096;

//	send_full (&whoami, sizeof(whoami));
	SAFE(send_intro(sock_client, &whoami));

	/* cmd loop */
	while (1) {
//		recv_full(&cmd, sizeof(cmd));
		SAFE(recv_command(sock_client, &cmd));

		switch (cmd.command) {
			case CMD_NOOP:
				log("ping");
				SAFE(send_reply_p(sock_client, cmd.id, STAT_OK, 0));
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
				SAFE(send_reply_p(sock_client, cmd.id, reply, 0));
				break;

			case CMD_LIST:
				// list directory - ha ha!
				VALIDATE_HANDLE(cmd);

				struct handle * h = handles[cmd.handle];

				if (h->dir) closedir(h->dir);
				h->dir = opendir(h->name);

		}
	}
}
