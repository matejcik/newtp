#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <gnutls/gnutls.h>
#include <gsasl.h>

#include "clientops.h"
#include "commands.h"
#include "common.h"
#include "log.h"
#include "structs.h"
#include "tools.h"

#define MYPORT "63987"
/* NEWTP on a phone */

void send_empty_command(int id, int command, int handle)
{
	pack_command_p(outbuf, id, 0, command, handle, 0);
	safe_send_full(outbuf, SIZEOF_command());
}

void recv_reply(struct reply * reply)
{
	safe_recv_full(inbuf, SIZEOF_reply());
	unpack_reply(inbuf, SIZEOF_reply(), reply);
	if (reply->length) safe_recv_full(inbuf + SIZEOF_reply(), reply->length);
}

#define ATTRIBUTES  "\x01\x06\x10\x02\x13"
#define ATTR_LEN    strlen(ATTRIBUTES)
#define ATTR_FORMAT "clcli"
#define ATTR_LIST_SIZE   (1 + 8 + 1 + 8 + 4)

void print_listing (char * data, int len)
{
	struct dir_entry entry;
	uint16_t items;
	int pos = 2;
	uint8_t  type, rights;
	uint64_t mtime, size;
	uint32_t uid;
	unsigned int st_mode;
	char date[30];
	time_t time;
	struct tm tm;

	unpack(data, len, "s", &items);

	while (items && pos < len) {
		char parms[3] = "---";
		if (unpack_dir_entry(data + pos, len - pos, &entry) == -1) {
			fprintf(stderr, "malformed entry in directory listing, attempting to skip\n");
			continue;
		}
		assert(entry.attr_len == ATTR_LIST_SIZE);
		unpack(entry.attr, entry.attr_len, ATTR_FORMAT, &rights, &mtime, &type, &size, &uid);

		st_mode = type << 12;
		if (S_ISREG(st_mode)) parms[0] = '-';
		else if (S_ISDIR(st_mode)) parms[0] = 'd';
		else if (S_ISCHR(st_mode)) parms[0] = 'c';
		else if (S_ISBLK(st_mode)) parms[0] = 'b';
		else parms[0] = '?';

		if (rights & RIGHTS_READ) parms[1] = 'r';
		if (rights & RIGHTS_WRITE) parms[2] = 'w';

		time = (time_t)mtime / 1000000;
		localtime_r(&time, &tm);
		strftime(date, 30, "%F %T", &tm);

		printf("%.3s %6d %10llu %s %.*s\n", parms, uid, (unsigned long long)size, date,
			entry.name_len, entry.name);

		pos += SIZEOF_dir_entry(&entry);
		free(entry.name);
		free(entry.attr);
		items--;
	}
	assert(pos <= len);
}

void do_assign (char * path, int handle)
{
	struct reply reply;
	int len = strlen(path);

	pack_command_p(inbuf, 1, 0, CMD_ASSIGN, handle, len);
	strncpy(inbuf + SIZEOF_command(), path, len);
	safe_send_full(inbuf, SIZEOF_command() + len);

	recv_reply(&reply);
	assert(reply.request_id == 1);
	if (reply.length > 0) safe_skip_data(reply.length);

	if (reply.result != STAT_OK) {
		fprintf(stderr, "failed to assign handle\n");
		exit(1);
	}
}

void do_list (char * path)
{
	struct reply reply;
	int id = 2;
	int end = 0;

	do_assign(path, 1);
	send_empty_command(2, CMD_REWINDDIR, 1);

	pack_command_p(outbuf, ++id, 0, CMD_READDIR, 1, ATTR_LEN);
	strcpy(outbuf + SIZEOF_command(), ATTRIBUTES);
	safe_send_full(outbuf, SIZEOF_command() + ATTR_LEN);

	/* read the REWINDDIR reply packet */
	recv_reply(&reply);
	assert(reply.request_id == 2);
	assert(reply.result == STAT_OK);

	/* start reading entries */
	do {
		recv_reply(&reply);
		assert(reply.request_id == id);
		switch (reply.result) {
			case STAT_FINISHED:
				end = 1;
			case STAT_CONTINUED:
				print_listing(inbuf + SIZEOF_reply(), reply.length);
				if (!end) {
					pack_command_p(outbuf, ++id, 0, CMD_READDIR, 1, ATTR_LEN);
					safe_send_full(outbuf, SIZEOF_command() + ATTR_LEN);
				}
				break;
			default:
				fprintf(stderr, "listing '%s' failed: %d\n", path, reply.result);
				exit(1);
		}
	} while (!end);
}

void do_get (char * path, char * target, int overwrite)
{
	struct reply reply;
	char * buf = outbuf;
	int id = 2, rid = 1;
	long ofs = 0;
	int fd;
	const int len = MAX_LENGTH;

	do_assign(path, 1);

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
		buf += pack_command_p(buf, id++, 0, CMD_READ, 1, SIZEOF_params_offlen());
		buf += pack_params_offlen_p(buf, ofs, len);
		ofs += len;
	}
	safe_send_full(outbuf, buf - outbuf);

	while (1) {
		recv_reply(&reply);
		assert(reply.request_id > rid);
		rid = reply.request_id;
		if (reply.result != STAT_OK) {
			/* bail */
			fprintf(stderr, "read failed: 0x%x\n", reply.result);
			exit(1);
		}
		if (reply.length > 0) {
			buf = inbuf + SIZEOF_reply();
			int l = 0;
			do {
				int i = write(fd, buf + l, reply.length - l);
				if (i <= 0) {
					if (errno == EINTR) continue;
					else {
						fprintf(stderr, "failed to write to %s: %s\n", target, strerror(errno));
						exit(1);
					}
				}
				l += i;
			} while (l < reply.length);
		} else {
			close(fd);
			break;
		}
		buf = outbuf;
		buf += pack_command_p(buf, id++, 0, CMD_READ, 1, SIZEOF_params_offlen());
		buf += pack_params_offlen_p(buf, ofs, len);
		safe_send_full(outbuf, buf - outbuf);
		ofs += len;
	}
}

int sasl_callback(Gsasl * ctx, Gsasl_session * session, Gsasl_property prop)
{
	static char * password = NULL;
	switch (prop) {
		case GSASL_ANONYMOUS_TOKEN:
			gsasl_property_set(session, prop, "anonymous");
			return GSASL_OK;
		case GSASL_PASSCODE:
		case GSASL_PASSWORD:
		case GSASL_AUTHID:
			if (!password) {
				password = xmalloc(1000);
				printf("Enter password: ");
				fgets(password, 999, stdin);
				/* delete newline */
				password[strlen(password) - 1] = 0;
			}
			gsasl_property_set(session, prop, password);
			return GSASL_OK;
		case GSASL_SERVICE:
			gsasl_property_set(session, prop, "newtp");
			return GSASL_OK;
		case GSASL_HOSTNAME:
			gsasl_property_set(session, prop, "localhost");
			return GSASL_OK;
		default:
			return GSASL_NO_CALLBACK;
	}
}

void do_sasl_auth(Gsasl * ctx, struct intro * intro)
{
	Gsasl_session * session;
	struct reply reply;
	char const * mechanism = gsasl_client_suggest_mechanism(ctx, intro->authstr);
	char * response;
	size_t resp_size;
	int ret;

	if (mechanism) {
		logp("using mechanism %s", mechanism);
	} else {
		err("failed to suggest mechanism");
		exit(4);
	}

	gsasl_client_start(ctx, mechanism, &session);
	pack_command_p(outbuf, 0, EXT_INIT, SASL_START, 0, strlen(mechanism));
	strcpy(outbuf + SIZEOF_command(), mechanism);
	safe_send_full(outbuf, SIZEOF_command() + strlen(mechanism));

	while (1) {
		recv_reply(&reply);
		assert(reply.extension == EXT_INIT);
		if (reply.result == SASL_R_CHALLENGE) {
			ret = gsasl_step(session, inbuf + SIZEOF_reply(), reply.length, &response, &resp_size);
			/* if we are done here, we still need to send empty(maybe?) response? */
			if (ret != GSASL_OK && ret != GSASL_NEEDS_MORE) {
				errp("SASL error %d: %s", ret, gsasl_strerror(ret));
				exit(4);
			}

			pack_command_p(outbuf, 0, EXT_INIT, SASL_RESPONSE, 0, resp_size);
			memcpy(outbuf + SIZEOF_command(), response, resp_size);
			free(response);
			safe_send_full(outbuf, SIZEOF_command() + resp_size);
		} else break;
	}

	if (reply.result != SASL_R_SUCCESS && reply.result != SASL_R_SUCCESS_OPT) {
		errp("authentication failed (0x%02x)", reply.result);
		exit(4);
	}
	gsasl_finish(session);
}

int main (int argc, char **argv)
{
	struct intro intro;
	char * command, * path, * target;
	Gsasl * ctx;

	setlocale(LC_ALL, "");
	assert(gsasl_init(&ctx) == GSASL_OK);
	gsasl_callback_set(ctx, sasl_callback);

	if (argc < 3) {
		printf("usage: %s <address> <command> [path]\n", argv[0]);
		return 0;
	}

	command = argv[2];
	if (argc > 3) path = argv[3];
	else path = "";

	if (newtp_client_connect(argv[1], MYPORT, &intro, NULL) > 0) return 1;
	do_sasl_auth(ctx, &intro);

	/*** perform actual commands ***/

	if (!strcmp("list", command)) {
		do_list(path);
	} else if (!strcmp("get", command)) {
		char * c = path;
		target = path;
		while (*c++) if (*c == '/') target = c + 1; /* get basename */
		do_get(path, target, 0);
	} else {
		fprintf(stderr, "unknown command: %s\n", command);
		exit(1);
	}

	newtp_gnutls_disconnect(1);
	gsasl_done(ctx);

	return 0;
}
