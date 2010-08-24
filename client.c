#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "commands.h"
#include "common.h"
#include "structs.h"
#include "tools.h"

#define MYPORT "5438"
/* LIFT on a phone */

int main (int argc, char **argv)
{
	struct addrinfo hints;
	struct addrinfo *res;
	struct intro intro;
	struct reply reply;
	int sock;

	if (argc < 2) {
		printf("usage: %s <address>\n", argv[0]);
		return 0;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(argv[1], MYPORT, &hints, &res)) {
		puts("error resolving address");
		return 1;
	}
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (connect(sock, res->ai_addr, res->ai_addrlen)) {
		printf("failed to connect: %s\n", strerror(errno));
		return 2;
	}

	recv_intro(sock, &intro);
	printf("server version %d, max %d handles, max %d chars per handle\n", intro.version, intro.maxhandles, intro.handlelen);
	send_command_p(sock, 55, CMD_NOOP, 0);
	recv_reply(sock, &reply);
	printf("server responds to ping!\n");
	assert(reply.id == 55);
	assert(reply.status == STAT_OK);
	assert(reply.datasize == 0);

	send_command_p(sock, 1, CMD_ASSIGN, 0);
	send_data(sock, "/", 1);
	recv_reply(sock, &reply);
	assert(reply.id == 1);
	assert(reply.status == STAT_OK);
	assert(reply.datasize == 0);

	close(sock);

	return 0;
}
