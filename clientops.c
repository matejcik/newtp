#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <gnutls/gnutls.h>
#include <gsasl.h>

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

void recv_reply (struct reply * reply)
{
	safe_recv_full(inbuf, SIZEOF_reply());
	unpack_reply(inbuf, SIZEOF_reply(), reply);
	if (reply->length) safe_recv_full(inbuf + SIZEOF_reply(), reply->length);
}

void reply_for_command (uint8_t extension, uint8_t command, uint16_t handle, uint16_t length, struct reply * reply)
{
	static uint16_t request_id = 0;

	pack_command_p(outbuf, request_id, extension, command, handle, length);
	safe_send_full(outbuf, length + SIZEOF_command());
	recv_reply(reply);
	assert(reply->request_id == request_id);

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

static int sasl_callback(Gsasl * ctx, Gsasl_session * session, Gsasl_property prop)
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

void newtp_client_sasl_auth (Gsasl * ctx, struct intro * intro)
{
	Gsasl_session * session;
	struct reply reply;
	char const * mechanism = gsasl_client_suggest_mechanism(ctx, intro->authstr);
	char * response;
	size_t resp_size;
	int ret;

	gsasl_callback_set(ctx, sasl_callback);

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
