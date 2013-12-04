#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <gnutls/gnutls.h>

#include "clientops.h"
#include "common.h"
#include "log.h"
#include "structs.h"
#include "commands.h"
#include "tools.h"

char * inbuf;
char * outbuf;
char * data_in;
char * data_out;

void reply_for_command (uint8_t extension, uint8_t command, uint16_t handle, uint16_t length, struct reply * reply)
{
	static uint16_t request_id = 0;

	pack_command_p(outbuf, request_id, extension, command, handle, length);
	safe_send_full(outbuf, length + SIZEOF_command());
	safe_recv_full(inbuf, SIZEOF_reply());
	unpack_reply(inbuf, SIZEOF_reply(), reply);
	assert(reply->request_id == request_id);
	if (reply->length) safe_recv_full(data_in, reply->length);

	++request_id;
}

int newtp_client_connect (char const * host, char const * port, struct intro * intro, char ** extensions)
{
	struct addrinfo hints;
	struct addrinfo *res;
	char * ext_ptr = NULL;
	int sock;
	uint16_t version;
	struct reply reply;

	memset(intro, 0, sizeof(struct intro));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, port, &hints, &res)) {
		err("error resolving address");
		return 1;
	}
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (connect(sock, res->ai_addr, res->ai_addrlen)) {
		errp("failed to connect: %s\n", strerror(errno));
		return 2;
	}

	inbuf    = xmalloc(MAX_LENGTH * 2);
	outbuf   = xmalloc(MAX_LENGTH * 2);
	data_out = outbuf + SIZEOF_command();
	data_in  = inbuf  + SIZEOF_reply();

	newtp_gnutls_init(sock, GNUTLS_CLIENT);

	/* perform protocol intro */
	pack(outbuf, "5Bs", "NewTP", (uint16_t)1);
	pack_command_p(outbuf + 7, 0xffff, EXT_INIT, INIT_WELCOME, 0, 0);
	safe_send_full(outbuf, 7 + SIZEOF_command());
	safe_recv_full(inbuf, 7 + SIZEOF_reply());
	if (strncmp(inbuf, "NewTP", 5)) {
		newtp_gnutls_disconnect(1);
		err("server sent invalid intro string, closing connetion");
		goto fail;
	}
	unpack(inbuf + 5, 2, "s", &version);
	assert(version == 1);
	unpack_reply(inbuf + 7, SIZEOF_reply(), &reply);

	assert(reply.length > 0);
	safe_recv_full(inbuf, reply.length);
	if (unpack_intro(inbuf, reply.length, intro) < 0) {
		err("server intro packet too short");
		newtp_gnutls_disconnect(1);
		goto fail;
	}
	assert(reply.request_id == 0xffff);
	assert(reply.extension == EXT_INIT && reply.result == R_OK);

	if (intro->num_extensions) {
		int ext_size = reply.length - SIZEOF_intro(intro);
		if (ext_size <= 0) {
			err("malformed intro packet");
			newtp_gnutls_disconnect(1);
			goto fail;
		}
		if (extensions) {
			*extensions = ext_ptr = xmalloc(ext_size);
			strncpyz(ext_ptr, inbuf + SIZEOF_intro(intro), ext_size);
		}
	}

	logp("server version %d on %s, max %d handles, max %d dirs",
		version, intro->platform, intro->max_handles, intro->max_opendirs);

	return 0;

fail:
	free(inbuf);
	free(outbuf);
	inbuf = outbuf = data_in = data_out = NULL;
	if (intro->platform) {
		free(intro->platform);
		free(intro->authstr);
		free(ext_ptr);
		if (extensions) *extensions = NULL;
	}
	return 3;
}
