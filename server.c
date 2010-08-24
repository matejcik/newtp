#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#define MYPORT "5438"	/* the port users will be connecting to */
#define BACKLOG 10	/* how many pending connections queue will hold */

int sock_server;
int sock_client;

void work (void);

void fork_client (int client)
{
	switch (fork()) {
		case 0: /* we're the child! */
			close(sock_server);
			sock_client = client;
			work();
			exit(0);
		case -1: /* fork failed! */
			fprintf(stderr, "%s\n", strerror(errno));
			break;
		
		default: /* we're the parent! */
			/* do nothing? */
			break;
	}
}

static void at_exit ()
{
	close(sock_server);
	fprintf(stderr, "closed server\n");
}

void sighandler (int signal)
{
	exit(0);
}

int main (void)
{
	struct sockaddr_storage remote_addr;
	socklen_t remote_addr_s = sizeof(remote_addr);
	struct addrinfo hints, *res;
	int yes = 1;

	atexit(at_exit);
	signal(SIGINT, sighandler);

	/* !! don't forget your error checking for these calls !! */

	/* first, load up address structs with getaddrinfo(): */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; /* use IPv4 or IPv6, whichever */
	/* TODO try ipv6 and fall back to ipv4 - because getaddrinfo returns ipv4 first :/ */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; /* fill in my IP for me */

	getaddrinfo(NULL, MYPORT, &hints, &res);

	/* make a socket, bind it, and listen on it: */

	sock_server = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	int err = bind(sock_server, res->ai_addr, res->ai_addrlen);
	if (err) {
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	err = setsockopt(sock_server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (err) {
		fprintf(stderr, "%s\n", strerror(errno));
		return 1;
	}
	listen(sock_server, BACKLOG);

	/* now accept an incoming connection: */
	while (1) {
		int sock = accept(sock_server, (struct sockaddr *)&remote_addr, &remote_addr_s);
		if (sock > 0) fork_client(sock);
		else fprintf(stderr, "%s\n", strerror(errno));
	}
	return 0;
}
