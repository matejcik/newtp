#include <assert.h>
#include <errno.h>
#include <locale.h>
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

#include <gsasl.h>
#include <gnutls/gnutls.h>

#include "commands.h"
#include "common.h"
#include "log.h"
#include "operations.h"
#include "paths.h"
#include "structs.h"
#include "tools.h"

#define MYPORT "63987"	/* the port users will be connecting to */
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

char * SASL_password = NULL;
Gsasl * SASL_context = NULL;

char * inbuf;
char * outbuf;

/***** server termination handling *****/

void close_server_sockets ()
{
	for (int i = 0; i < socknum; i++)
		close(sockets[i]);
}

void at_exit ()
{
	if (child) close(childsock);
	else close_server_sockets();
	if (SASL_context) gsasl_done(SASL_context);
}

void sighandler (int signal)
{
	printf("\n");
	exit(0); /* invokes at_exit handler */
}


/***** child process functions *****/

#define HANDLE_CMD(x) \
	case CMD_##x:\
		len = cmd_##x(&cmd, inbuf, outbuf); \
		break;

void do_work ()
{
	struct command cmd;
	int len;

	while (1) {
		safe_recv_full(inbuf, SIZEOF_command());
		unpack_command(inbuf, SIZEOF_command(), &cmd);
		if (cmd.length) safe_recv_full(inbuf, cmd.length);

		logp("received command: request_id 0x%04x, ext 0x%02x, cmd 0x%02x, length %d",
			cmd.request_id, cmd.extension, cmd.command, cmd.length);

		len = 0;

		switch (cmd.command) {
			HANDLE_CMD(ASSIGN)
			HANDLE_CMD(STAT)
			HANDLE_CMD(SETATTR)
			HANDLE_CMD(STATVFS)
			HANDLE_CMD(READ)
			HANDLE_CMD(WRITE)
			HANDLE_CMD(TRUNCATE)
			HANDLE_CMD(DELETE)
			HANDLE_CMD(RENAME)
			HANDLE_CMD(MAKEDIR)
			HANDLE_CMD(REWINDDIR)
			HANDLE_CMD(READDIR)
			default:
				logp("unknown command: %x", cmd.command);
				len = pack_reply_p(outbuf, cmd.request_id, 0, ERR_BADCOMMAND, 0);
				break;
		}

		if (len == 0) {
			len = pack_reply_p(outbuf, cmd.request_id, 0, ERR_FAIL, 0);
		}

		if (len > 0) {
			safe_send_full(outbuf, len);
		} else {
			len = pack_reply_p(outbuf, cmd.request_id, 0, ERR_SERVFAIL, 0);
			safe_send_full(outbuf, len);
		}
	}
}

void do_sasl_auth ()
{
	Gsasl_session * session = NULL;
	char * mechanism, * response = NULL, * output = NULL;
	uint16_t mech_len = 0, resp_len = 0;
	size_t output_len = 0;
	struct command cmd;
	int res, err, ext = EXT_INIT;

	safe_recv_full(inbuf, SIZEOF_command());
	unpack_command(inbuf, SIZEOF_command(), &cmd);
	if (cmd.length) safe_recv_full(inbuf, cmd.length);
	if (cmd.extension != EXT_INIT || (cmd.command != SASL_START && cmd.command != SASL_START_OPT)) {
		err = SASL_E_FAILED;
		goto fail;
	}

	if (cmd.command == SASL_START) {
		inbuf[cmd.length] = 0;
		mechanism = inbuf;
	} else {
		res = unpack(inbuf, cmd.length, "sBsB", &mech_len, &mechanism, &resp_len, &response);
		if (res < 0) {
			ext = 0; err = ERR_BADPACKET;
			goto fail;
		}
	}

	logp("client uses mechanism '%s'", mechanism);
	if (SASL_password && !strcmp(mechanism, "ANONYMOUS")) {
		err = SASL_E_BAD_MECHANISM;
		err("but anonymous login is forbidden");
		goto fail;
	}
	
	res = gsasl_server_start(SASL_context, mechanism, &session);
	if (mechanism != inbuf) free(mechanism);
	

	if (res != GSASL_OK) {
		if (res == GSASL_UNKNOWN_MECHANISM) err = SASL_E_BAD_MECHANISM;
		else err = ERR_SERVFAIL;
		goto fail;
	}

	if (SASL_password) {
		gsasl_property_set(session, GSASL_AUTHID, SASL_password);
		gsasl_property_set(session, GSASL_PASSWORD, SASL_password);
		gsasl_property_set(session, GSASL_SERVICE, "newtp");
		gsasl_property_set(session, GSASL_HOSTNAME, "localhost");
	}

	/* start the session (response might be NULL) */
	res = gsasl_step(session, response, resp_len, &output, &output_len);
	free(response);
	while (res == GSASL_NEEDS_MORE) {
		pack_reply_p(outbuf, cmd.request_id, EXT_INIT, SASL_R_CHALLENGE, (uint16_t)output_len);
		memcpy(outbuf + SIZEOF_reply(), output, output_len);
		safe_send_full(outbuf, SIZEOF_reply() + output_len);
		free(output);

		safe_recv_full(inbuf, SIZEOF_command());
		unpack_command(inbuf, SIZEOF_command(), &cmd);
		if (cmd.length) safe_recv_full(inbuf, cmd.length);
		if (cmd.extension != EXT_INIT || cmd.command != SASL_RESPONSE) {
			err = SASL_E_FAILED;
			goto fail;
		}

		res = gsasl_step(session, inbuf, cmd.length, &output, &output_len);
	}

	if (res == GSASL_OK) {
		pack_reply_p(outbuf, cmd.request_id, EXT_INIT, SASL_R_SUCCESS_OPT, (uint16_t)output_len);
		memcpy(outbuf + SIZEOF_reply(), output, output_len);
		safe_send_full(outbuf, SIZEOF_reply() + output_len);
		free(output);
		gsasl_finish(session);
		log("SASL authentication succeeded");
		return;
	}

	if (res == GSASL_SASLPREP_ERROR || res == GSASL_UNICODE_NORMALIZATION_ERROR ||
	    res == GSASL_MECHANISM_PARSE_ERROR || res == GSASL_INTEGRITY_ERROR)
		err = SASL_E_BAD_MESSAGE;
	else if (res == GSASL_AUTHENTICATION_ERROR)
		err = SASL_E_FAILED;
	else
		err = ERR_SERVFAIL;
	/* proceed to fail */
fail:
	errp("SASL authentication failed (0x%02x)", err);
	if (session) gsasl_finish(session);
	pack_reply_p(outbuf, cmd.request_id, ext, err, 0);
	safe_send_full(outbuf, SIZEOF_reply());
	newtp_gnutls_disconnect(1);
	exit(4);
}

char * sasl_mechanisms ()
{
	char * mechanisms = NULL;
	char * anon = 0;
	if (!SASL_password) {
		log("using the ANONYMOUS mechanism");
		mechanisms = xmalloc(strlen("ANONYMOUS") + 1);
		strcpy(mechanisms, "ANONYMOUS");
		return mechanisms;
	}
	gsasl_server_mechlist(SASL_context, &mechanisms);
	if (!mechanisms) {
		err("failed to list mechanisms");
		return NULL;
	} else {
		anon = strstr(mechanisms, "ANONYMOUS");
		if (anon) {
			int a = strlen("ANONYMOUS");
			while (anon[a]) {
				anon[0] = anon[a];
				anon++;
			}
		}
		logp("using the following mechanisms: %s", mechanisms);
		return mechanisms;
	}
}

int sasl_callback (Gsasl * ctx, Gsasl_session * sctx, Gsasl_property prop)
{
	if (prop == GSASL_VALIDATE_ANONYMOUS) return GSASL_OK;
	return GSASL_NO_CALLBACK;
}

void do_session_init()
{
	uint16_t length, version;
	struct intro intro;
	struct command cmd;

	/* wait for client intro */
	safe_recv_full(inbuf, 7 + SIZEOF_command());
	/* client intro should be "NewTP" - length - version */
	if (strncmp("NewTP", inbuf, 5)) {
		err("invalid client intro string");
		exit(1);
	}
	unpack(inbuf + 5, 2, "s", &version);
	logp("client connected, version %d", version);

	unpack_command(inbuf + 7, SIZEOF_command(), &cmd);
	/* ignore arguments after version */
	if (cmd.extension != EXT_INIT || cmd.command != INIT_WELCOME) {
		err("invalid intro packet");
		exit(1);
	}
	if (cmd.length > 0) safe_skip_data(cmd.length);

	/* do not check version because we can't do anything with it, this is v1 */

	/* intro data */
	intro.max_handles = handle_init();
	intro.max_opendirs = MAX_OPENDIRS;
	intro.platform_len = 5;
	intro.platform = "posix";
	intro.authstr = sasl_mechanisms();
	intro.authstr_len = intro.authstr ? strlen(intro.authstr) : 0;
	intro.num_extensions = 0;

	length  = pack(outbuf, "5Bs", "NewTP", (uint16_t)1);
	length += pack_reply_p(outbuf + length, cmd.request_id, EXT_INIT, R_OK, SIZEOF_intro(&intro));
	length += pack_intro(outbuf + length, &intro);
	assert(length == 7 + SIZEOF_reply() + SIZEOF_intro(&intro));
	free(intro.authstr);

	safe_send_full(outbuf, length);

	log("session initialized");
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
	log("connection received");

	/* initialize TLS */
	newtp_gnutls_init (client, GNUTLS_SERVER);

	inbuf = xmalloc(MAX_LENGTH * 2);
	outbuf = xmalloc(MAX_LENGTH * 2); /* needed? */

	do_session_init();
	do_sasl_auth();
	do_work();

	newtp_gnutls_disconnect(1);
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

	setlocale(LC_ALL, "");

	atexit(&at_exit);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGSEGV, sighandler);

	/* process command line arguments */
	if (argc < 2) {
		printf("usage: %s [-p password] <shares>\n", argv[0]);
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
		} else if (!strcmp("-p", argv[i])) {
			if (argc > i + 1) {
				SASL_password = argv[i+1];
				i++;
				continue;
			} else {
				printf("-p specified but no password supplied\n");
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

	/* set up Gsasl context */
	if ((err = gsasl_init(&SASL_context)) != GSASL_OK) {
		printf("failed to initialize SASL (%d): %s\n",
			err, gsasl_strerror(err));
		return 1;
	}
	gsasl_callback_set(SASL_context, sasl_callback);

	if (!SASL_password) {
		if (!gsasl_server_support_p(SASL_context, "ANONYMOUS")) {
			printf("SASL does not support ANONYMOUS mechanism\n");
			return 2;
		}
		printf("Note: running in ANONYMOUS mode\n");
	} else if (!gsasl_server_support_p(SASL_context, "PLAIN") &&
		   !gsasl_server_support_p(SASL_context, "SCRAM-SHA-1")) {
		printf("SASL does not support suitable password-based mode\n");
		return 2;
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
