#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "commands.h"
#include "common.h"
#include "log.h"
#include "structs.h"
#include "tools.h"

#define MYPORT "5438"
/* LIFT on a phone */

void print_listing (char * data, int len)
{
	struct dir_entry entry;
	int pos = 0;
	while (pos < len) {
		char parms[3] = "---";
		unpack(data + pos, FORMAT_dir_entry, &entry.len, &entry.name, &entry.type, &entry.perm, &entry.size);
		if (entry.type == ENTRY_DIR) parms[0] = 'd';
		else if (entry.type == ENTRY_OTHER) parms[0] = '?';
		if (entry.perm & PERM_READ) parms[1] = 'r';
		if (entry.perm & PERM_WRITE) parms[2] = 'w';
		printf("%.3s %.*s\t\t%llu\n", parms, entry.len, entry.name, entry.size);
		pos += SIZEOF_dir_entry - sizeof(char *) + entry.len;
		free(entry.name);
	}
	assert(pos == len);
}

void do_assign (int sock, char * path, int handle)
{
	struct reply reply;
	SAFE(send_command_p(sock, 1, CMD_ASSIGN, handle));
	SAFE(send_data(sock, path, strlen(path)));
	SAFE(recv_reply(sock, &reply));
	assert(reply.id == 1);
	if (reply.status != STAT_OK) {
		fprintf(stderr, "failed to assign handle\n");
		exit(1);
	}
}

void do_list (int sock, char * path)
{
	struct reply reply;
	char * data;
	int len;
	int id = 2;
	int end = 0;

	do_assign(sock, path, 1);

	SAFE(send_command_p(sock, id, CMD_LIST, 1));
	do {
		SAFE(recv_reply(sock, &reply));
		assert(reply.id == id);
		switch (reply.status) {
			case STAT_OK:
				end = 1;
			case STAT_CONT:
				SAFE(recv_length(sock, &len));
				if (len > 0) {
					data = xmalloc(len);
					SAFE(recv_full(sock, data, len));
					print_listing(data, len);
					free(data);
				}
				if (!end) SAFE(send_command_p(sock, ++id, CMD_LIST_CONT, 1));
				break;
			default:
				fprintf(stderr, "listing '%s' failed: %d\n", path, reply.status);
				exit(1);
		}
	} while (!end);
}

void do_get (int sock, char * path, char * target, int overwrite)
{
	struct reply reply;
	char * data;
	int dlen;
	int id = 2, rid = 1;
	long ofs = 0;
	int fd;
	const int len = 65536;

	do_assign(sock, path, 1);

	/* TODO ensure that the file exists and is readable on server side
	 before creating the local file */

	fd = open(target, O_WRONLY | O_CREAT | (overwrite ? O_TRUNC : 0), 0644);
	if (fd == -1) {
		fprintf(stderr, "failed to open %s: %s\n", target, strerror(errno));
		exit(1);
	}
	ofs = lseek(fd, 0, SEEK_END);

	/* simplistic-smart approach: send many reads at once, for each success
	 * send a next one. exit when first read fails/shorts. */
	for (int i = 0; i < 8; i++) {
		SAFE(send_command_p(sock, id++, CMD_READ, 1));
		SAFE(send_params_offlen_p(sock, ofs, len));
		ofs += len;
	}
	while (1) {
		SAFE(recv_reply(sock, &reply));
		assert(reply.id > rid);
		rid = reply.id;
		if (reply.status != STAT_OK) {
			/* bail */
			fprintf(stderr, "read failed: %x\n", reply.status);
			exit(1);
		}
		SAFE(recv_length(sock, &dlen));
		if (dlen > 0) {
			data = xmalloc(dlen);
			SAFE(recv_full(sock, data, dlen));
			int l = 0;
			do {
				int i = write(fd, data + l, dlen - l);
				if (i <= 0) {
					if (errno == EINTR) continue;
					else {
						fprintf(stderr, "failed to write to %s: %s\n", target, strerror(errno));
						exit(1);
					}
				}
				l += i;
			} while (l < dlen);
			free(data);
		} else {
			close(fd);
			break;
		}
		SAFE(send_command_p(sock, id++, CMD_READ, 1));
		SAFE(send_params_offlen_p(sock, ofs, len));
		ofs += len;
	}
}

void do_put (int sock, char * local, char * remote)
{
}

int main (int argc, char **argv)
{
	struct addrinfo hints;
	struct addrinfo *res;
	struct intro intro;
	struct reply reply;
	char * command, * path, * target;
	int sock;

	if (argc < 3) {
		printf("usage: %s <address> <command> [path]\n", argv[0]);
		return 0;
	}

	command = argv[2];
	if (argc > 3) path = argv[3];
	else path = "";

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
	fprintf(stderr, "server version %d, max %d handles, max read of %d\n", intro.version, intro.maxhandles, intro.maxdata);
	send_command_p(sock, 55, CMD_NOOP, 0);
	recv_reply(sock, &reply);
	fprintf(stderr, "server responds to ping!\n");
	assert(reply.id == 55);
	assert(reply.status == STAT_OK);

	if (!strcmp("list", command)) {
		do_list(sock, path);
	} else if (!strcmp("get", command)) {
		char * c = path;
		target = path;
		while (*c++) if (*c == '/') target = c + 1;
		do_get(sock, path, target, 0);
	} else if (!strcmp("put", command)) {
		if (argc < 5) {
			printf("usage: %s %s put <file> [file...] <path>\n", argv[0], argv[1]);
			return 1;
		}
		for (int i = 3; i < argc - 1; i++) {
			do_put(sock, argv[i], argv[argc-1]);
		}
	} else {
		fprintf(stderr, "unknown command: %s\n", command);
		exit(1);
	}

	close(sock);

	return 0;
}
