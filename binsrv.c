#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

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

struct intro whoami;

#define MAXHANDLES 16
char* handles[MAXHANDLES];

int validate_assign_handle (uint16_t handle, struct data_packet *pkt)
{
#define SLASH 1
#define DOTS 2
#define OTHER 0
	int state = OTHER;
	char* c = pkt->data;
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

	if (handles[handle]) free(handles[handle]);
	handles[handle] = xmalloc(pkt->len + 2);
	handles[handle][0] = '.';
	handles[handle][pkt->len + 1] = 0;
	strncpy(handles[handle] + 1, pkt->data, pkt->len);
	
	return STAT_OK;
}

void work (void)
{
	struct command cmd;
	struct data_packet pkt;
	int reply;

	log("connection received");

	// clear handles
	for (int i = 0; i < MAXHANDLES; i++) handles[i] = 0;

	/* intro */
	whoami.version = 0;
	whoami.maxhandles = MAXHANDLES;
	whoami.handlelen = 4096;

//	send_full (&whoami, sizeof(whoami));
	send_intro(sock_client, &whoami);

	/* cmd loop */
	while (1) {
//		recv_full(&cmd, sizeof(cmd));
		recv_command(sock_client, &cmd);

		switch (cmd.command) {
			case CMD_NOOP:
				log("ping");
				send_reply_p(sock_client, cmd.id, STAT_OK, 0);
				break;

			case CMD_ASSIGN:
				// read data
				recv_data(sock_client, &pkt);

				reply = validate_assign_handle(cmd.handle, &pkt);
				if (reply == STAT_OK) {
					logp("assigning to handle %d, name '%s'", cmd.handle, handles[cmd.handle]);
				}
				send_reply_p(sock_client, cmd.id, reply, 0);
				break;
		}
	}
}
