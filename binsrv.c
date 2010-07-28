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

int send_full (void *data, int len)
{
	const char *buf = (const char *) data;
	int origlen = len;
	while (1) {
		int wrote = send(sock_client, buf, len, 0);
		if (wrote == len) return origlen;
		else if (wrote < 0) {
			if (errno == EINTR) continue;
			else return -1;
		}
		else if (wrote == 0) return 0;
		else {
			buf += wrote;
			len -= wrote;
		}
	}
}

#define SAFE(x) { \
	int HANDLEret = x; \
	if (HANDLEret == -1) { \
		errp("in ##x : %s", strerror(errno)); \
		die(); \
	} else if (HANDLEret == 0) { \
		log("connection lost"); \
		die(); \
	} \
}

void send_intro (struct intro *intro)
{
	intro->version = htons(intro->version);
	intro->maxhandles = htons(intro->maxhandles);
	intro->handlelen = htons(intro->handlelen);
	SAFE(send_full(intro, sizeof(struct intro)));
}

int recv_full (void *data, int len)
{
	char *buf = (char *) data;
	int orig_len = len;
	while (1) {
		int read = recv(sock_client, buf, len, 0);
		if (read < 0) {
			if (errno == EINTR) continue;
			else return -1; /* error */
		}
		if (read == 0) return 0; /* connection reset by peer */
		len -= read;
		buf += read;
		if (len <= 0) return orig_len; /* done */
	}
}

void recv_command (struct cmd_payload *cmd)
{
	SAFE(recv_full(cmd, sizeof(struct cmd_payload)));
	cmd->id = ntohs(cmd->id);
	cmd->command = ntohs(cmd->command);
	cmd->handle = ntohs(cmd->handle);
}

void recv_data (struct data_packet *pkt)
{
	SAFE(recv_full(&pkt->len, sizeof(uint32_t)));
	pkt->data = xmalloc(pkt->len);
	SAFE(recv_full(pkt->data, pkt->len));
}

void send_reply (struct cmd_payload *cmd, uint8_t status, uint16_t datasize)
{
	struct reply_payload reply;
	reply.id = htons(cmd->id);
	reply.status = status;
	reply.datasize = htons(datasize);
	SAFE(send_full(&reply, sizeof(struct reply_payload)));
}

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
	struct cmd_payload cmd;
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
	send_intro(&whoami);

	/* cmd loop */
	while (1) {
//		recv_full(&cmd, sizeof(cmd));
		recv_command(&cmd);

		switch (cmd.command) {
			case CMD_NOOP:
				log("ping");
				send_reply(&cmd, STAT_OK, 0);
				break;

			case CMD_ASSIGN:
				// read data
				recv_data(&pkt);

				reply = validate_assign_handle(cmd.handle, &pkt);
				if (reply == STAT_OK) {
					logp("assigning to handle %d, name '%s'", cmd.handle, handles[cmd.handle]);
				}
				send_reply(&cmd, reply, 0);
				break;
		}
	}
}
