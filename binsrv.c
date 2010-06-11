#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "commands.h"
#include "structs.h"
#include "log.h"

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

int send_full (void *buf, int len)
{
//	const char *buf = (const char *) data;
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
	if (HANDLEret == -1) errp("in ##x : %s", sys_errlist[errno]); \
	else log("connection lost"); \
	die(); \
}

void send_intro (struct intro *intro)
{
	intro->version = htons(intro->version);
	intro->maxhandles = htons(intro->maxhandles);
	intro->handlelen = htons(intro->handlelen);
	SAFE(send_full(intro, sizeof(struct intro)));
}

int recv_full (void *buf, int len)
{
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

void send_reply (uint16_t id, uint8_t status, uint16_t datasize)
{
	struct reply_payload reply;
	reply.id = htons(id);
	reply.status = status;
	reply.datasize = htons(datasize);
	SAFE(send_full(&reply, sizeof(struct reply_payload)));
}

void work (void)
{
	struct cmd_payload cmd;
	log("connection received");

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

		switch (cmd->command) {
			case CMD_NOOP:
				log("ping");
				send_reply(cmd->id, STAT_OK, 0, 0);
				break;

			case CMD_ASSIGN:
				// read data
		}
	}
}
