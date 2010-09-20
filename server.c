#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "commands.h"
#include "common.h"
#include "log.h"
#include "operations.h"
#include "paths.h"
#include "structs.h"

#define MYPORT "5438"	/* the port users will be connecting to */
#define BACKLOG 10	/* how many pending connections queue will hold */

#define CHECK(err,call,ret) { \
	err = call; \
	if (err == -1) { perror(#call); ret; } \
}

#define SERVERS 10
int sockets[SERVERS];
int socknum = 0;

int child = 0;
int childsock;

void close_server_sockets ()
{
	for (int i = 0; i < socknum; i++)
		close(sockets[i]);
}

void at_exit ()
{
	if (child) close(childsock);
	else close_server_sockets();
}

void sighandler (int signal)
{
	printf("\n");
	exit(0); /* invokes at_exit handler */
}

void work (int sock)
{
	struct command cmd;

	log("connection received");

	SAFE(server_init(sock));

	while (1) {
		SAFE(recv_command(sock, &cmd));

		switch (cmd.command) {
			case CMD_NOOP: /* server ping - implemented right here */
				log("ping");
				SAFE(send_reply_p(sock, cmd.id, STAT_OK));
				break;

			case CMD_ASSIGN: /* assign a handle */
				SAFE(cmd_assign(sock, &cmd));
				break;
			case CMD_LIST:
				SAFE(cmd_list(sock, &cmd));
				break;
			case CMD_LIST_CONT:
				SAFE(cmd_list_cont(sock, &cmd));
				break;
			case CMD_READ:
				SAFE(cmd_read(sock, &cmd));
				break;
			case CMD_WRITE:
				SAFE(cmd_write(sock, &cmd));
				break;
			case CMD_DELETE:
				SAFE(cmd_delete(sock, &cmd));
				break;
			case CMD_RENAME:
				SAFE(cmd_rename(sock, &cmd));
				break;
			default:
				logp("unknown command: %x", cmd.command);
				SAFE(send_reply_p(sock, cmd.id, STAT_BADCMD));
				break;
		}
	}
}

void fork_client (int client)
{
	int pid;
	CHECK(pid, fork(), return);

	if (pid > 0) return;

	/* we're the child! */
	child = 1;
	childsock = client;
	close_server_sockets();
	work(childsock);
	exit(0);
}

int main (int argc, char ** argv)
{
	struct addrinfo hints, *res;
	struct sockaddr_storage remote_addr;
	socklen_t remote_addr_s = sizeof(remote_addr);
	int yes = 1;
	int sock, s, err, max = -1;
	fd_set set;

	atexit(&at_exit);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGSEGV, sighandler);

	/* process command line arguments */
	if (argc < 2) {
		printf("usage: %s <shares>\n", argv[0]);
		printf("shares can be specified as follows:\n");
		printf("/path/to/share=name - this share is read-only\n");
		printf("-ro /path/to/share=name - this is also read-only\n");
		printf("-rw /path/to/share=name - this is read-write\n");
		printf("example: %s -ro /home/you/Public=public -rw /home/you/Incoming=Incoming\n", argv[0]);
		exit(1);
	}
	for (int i = 1; i < argc; i++) {
		char * path;
		char * name;
		int writable = 0;
		if (!strcmp("-ro", argv[i])) {
			if (++i < argc) path = argv[i];
			else {
				printf("missing share name for -ro\n");
				exit(1);
			}
		} else if (!strcmp("-rw", argv[i])) {
			writable = 1;
			if (++i < argc) path = argv[i];
			else {
				printf("missing share name for -rw\n");
				exit(1);
			}
		} else {
			path = argv[i];
		}
		name = path;
		while (*name && *name != '=') name++; /* find '=' */
		if (!*name) {
			printf("cannot find share name in string '%s'\n", path);
			exit(1);
		}
		*name = 0; /* break into path/name pair */
		name++;
		if (!*name) {
			printf("share name cannot be empty\n");
			exit(1);
		}
		/* TODO check for invalid share names */

		if (!share_add(name, path, writable)) {
			printf("failed to add '%s' under name '%s'\n", path, name);
		}
	}

	/* first, load up address structs with getaddrinfo(): */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; /* use IPv4 or IPv6, whichever */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; /* fill in my IP for me */

	CHECK(err,getaddrinfo(NULL, MYPORT, &hints, &res), return 1);

	/* make a socket, bind it, and listen on it: */
	for (; res; res = res->ai_next) {
		CHECK(s, socket(res->ai_family, res->ai_socktype, res->ai_protocol), continue);
		logp("bound %d to %d with %d", s, res->ai_family, res->ai_protocol);
		if (res->ai_family == AF_INET6) {
			CHECK(err, setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(int)), /*nothing*/);
		}
		CHECK(err, bind(s, res->ai_addr, res->ai_addrlen), continue);
		CHECK(err, setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)), /*nothing*/);
		CHECK(err, listen(s, BACKLOG), continue);

		assert(socknum < SERVERS);
		sockets[socknum] = s;
		++socknum;
	}

	FD_ZERO(&set);
	for (int i = 0; i < socknum; i++) {
		FD_SET(sockets[i], &set);
		if (sockets[i] > max) max = sockets[i];
	}
	logp("we have %d sockets, max is %d", socknum, max);
	
	while (1) {
		CHECK(err, select(max + 1, &set, NULL, NULL, NULL), continue);
		if (err <= 0) continue;

		for (int i = 0; i < socknum; i++) {
			if (FD_ISSET(sockets[i], &set)) {
				CHECK(sock, accept(sockets[i], (struct sockaddr *)&remote_addr, &remote_addr_s), continue);
				fork_client(sock);
			}
		}
	}
}
