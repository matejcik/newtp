#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gnutls/gnutls.h>

#include "common.h"
#include "log.h"
#include "structs.h"
#include "tools.h"

/* endian-independent htonll - assumes 8bit char */
uint64_t htonll (uint64_t hostlong)
{
	unsigned char buffer[8];
	uint64_t *ret = (uint64_t *)buffer;
	for (int i = 0; i < 8; i++) {
		buffer[i] = (uint8_t)((hostlong >> ((7 - i) * 8)) & ((uint64_t)0xff));
	}
	return *ret;
}

uint64_t ntohll (uint64_t netlong)
{
	uint64_t ret = 0;
	unsigned char *buffer = (unsigned char *)&netlong;
	for (int i = 0; i < 8; i++) {
		ret <<= 8;
		ret |= buffer[i];
	}
	return ret;
}

#define ISDIGIT(x) ((x) >= '0' && (x) <= '9')

int pack (char * const buffer, char const * format, ...)
{
	va_list ap;
	char * buf = buffer;
	char * string;
	int num; uint64_t lnum;
	uint8_t v8bit;
	uint16_t v16bit;
	uint32_t v32bit;
	uint64_t v64bit;
	uint16_t len;

	va_start(ap, format);

	while (*format) {
		switch (*format) {
			case 'c': /* 8bit char */
				num = va_arg(ap, unsigned int);
				v8bit = (uint8_t)num;
				*buf = v8bit;
				buf += 1;
				break;
			case 's': /* 16bit short */
				num = va_arg(ap, unsigned int);
				len = (uint16_t)num;
				v16bit = htons(len);
				memcpy(buf, &v16bit, 2);
				buf += 2;
				break;
			case 'i': /* 32bit int */
				num = va_arg(ap, unsigned int);
				v32bit = htonl((uint32_t)num);
				memcpy(buf, &v32bit, 4);
				buf += 4;
				break;
			case 'l': /* 64bit long */
				lnum = va_arg(ap, uint64_t);
				v64bit = htonll((uint64_t)lnum);
				memcpy(buf, &v64bit, 8);
				buf += 8;
				break;
			case 'B': /* byte string, length specified by previous short */
				string = va_arg(ap, char *);
				assert(format[-1] == 's' || ISDIGIT(format[-1]));
				if (len > 0) {
					assert(string);
					memcpy(buf, string, len);
				}
				buf += len;
				break;
			default:
				if (ISDIGIT(*format)) {
					len = *format - '0';
				} else {
					assert(0);
				}
		}
		++format;
	}

	va_end(ap);

	return buf - buffer;
}

int unpack (char const * const buffer, int available, char const * format, ...)
{
	va_list ap;
	char const * buf = buffer;
	char ** string;
	uint8_t * v8bit;
	uint16_t * v16bit;
	uint32_t * v32bit;
	uint64_t * v64bit;
	uint16_t len;

	va_start(ap, format);

	while (*format) {
		switch (*format) {
			case 'c': /* 8bit char */
				if (available < 1) return -1;
				v8bit = va_arg(ap, uint8_t *);
				*v8bit = *buf;
				buf += 1; available -= 1;
				break;
			case 's': /* 16bit short */
				if (available < 2) return -1;
				v16bit = va_arg(ap, uint16_t *);
				memcpy(v16bit, buf, 2);
				buf += 2; available -= 2;
				*v16bit = ntohs(*v16bit);
				len = *v16bit;
				break;
			case 'i': /* 32bit int */
				if (available < 4) return -1;
				v32bit = va_arg(ap, uint32_t *);
				memcpy(v32bit, buf, 4);
				buf += 4; available -= 4;
				*v32bit = ntohl(*v32bit);
				break;
			case 'l': /* 64bit long */
				if (available < 8) return -1;
				v64bit = va_arg(ap, uint64_t *);
				memcpy(v64bit, buf, 8);
				buf += 8; available -= 8;
				*v64bit = ntohll(*v64bit);
				break;
			case 'B': /* byte string, length specified by previous short */
				string = va_arg(ap, char **);
				assert(format[-1] == 's' || ISDIGIT(format[-1]));
				if (available < len) return -1;
				*string = xmalloc(len + 1);
				memcpy(*string, buf, len);
				(*string)[len] = 0;
				buf += len; available -= len;
				break;
			default:
				if (ISDIGIT(*format)) {
					len = *format - '0';
				} else {
					assert(0);
				}
		}
		++format;
	}

	va_end(ap);

	return buf - buffer;
}

/****** gnutls initialization and handling ******/

static gnutls_session_t _session;
static gnutls_anon_server_credentials_t _server_cred;
static gnutls_anon_client_credentials_t _client_cred;
static int _socket;

void newtp_gnutls_init (int socket, int flag)
{
	int ret;
	char const * err;

	_socket = socket;

	gnutls_global_init();
	gnutls_init(&_session, flag);
	ret = gnutls_priority_set_direct(_session,
		"NORMAL:"
		"+ANON-ECDH:"
		"-COMP-ALL:+COMP-NULL:"
		"-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1"
		, &err);
	if (ret != GNUTLS_E_SUCCESS) {
		errp("error %s", gnutls_strerror(ret));
		exit(6);
	}


	if (flag == GNUTLS_SERVER) {
		gnutls_anon_allocate_server_credentials(&_server_cred);
		gnutls_credentials_set(_session, GNUTLS_CRD_ANON, _server_cred);
	} else {
		gnutls_anon_allocate_client_credentials(&_client_cred);
		gnutls_credentials_set(_session, GNUTLS_CRD_ANON, _client_cred);
	}

	gnutls_transport_set_ptr(_session, (void*)(uintptr_t)socket);

	do { ret = gnutls_handshake(_session); } while (ret < 0 && gnutls_error_is_fatal(ret) == 0);
	if (ret < 0) {
		errp("TLS Handshake failed: %s", gnutls_strerror(ret));
		newtp_gnutls_disconnect(0);
		exit(6);
	}
}

void newtp_gnutls_disconnect (int bye)
{
	if (bye) gnutls_bye(_session, GNUTLS_SHUT_RDWR);
	close(_socket);
	gnutls_deinit(_session);
	gnutls_anon_free_server_credentials(_server_cred);
	gnutls_anon_free_client_credentials(_client_cred);
	gnutls_global_deinit();
}

int send_full (void *data, int len)
{
	assert(len > 0); /* because otherwise returns 0, which makes the caller think that connection dropped */
	const char *buf = (const char *) data;
	int total = 0;
	while (total < len) {
		int w = gnutls_record_send(_session, buf + total, len - total);
		if (w <= 0) return w;
		total += w;
	}
	return total;
}

int recv_full (void *data, int len)
{
	assert(len > 0); /* because otherwise returns 0, which makes the caller think that connection dropped */
	char *buf = (char *) data;
	int total = 0;
	while (total < len) {
		int r = gnutls_record_recv(_session, buf + total, len - total);
		if (r <= 0) return r;
		total += r;
	}
	return total;
}

int skip_data (int len)
{
#define SKIP_BUF 16384
	char buf[SKIP_BUF];
	int r;
	while (len > 0) {
		r = gnutls_record_recv(_session, buf, ((len > SKIP_BUF) ? SKIP_BUF : len));
		if (r <= 0) return r;
		len -= r;
	}
	return 1;
}

int continue_or_die (int err)
{
	if (err > 0) return err;
	if (err == 0) { /* peer has closed connection */
		log("lost connection to peer");
		newtp_gnutls_disconnect(1);
		log("disconnect done");
		exit(2);
	} else if (gnutls_error_is_fatal(err) == 0) {
		warnp("TLS warning: %s", gnutls_strerror(err));
		return 0;
	} else {
		errp("TLS error: %s", gnutls_strerror(err));
		err("closing connection");
		newtp_gnutls_disconnect(0);
		exit(1);
	}
}

int safe_send_full (void *data, int len)
{
	int ret;
	/* zero indicates non-fatal failure, retry the call */
	while ((ret = continue_or_die(send_full(data, len))) == 0) { }
	return ret;
}

int safe_recv_full (void *data, int len)
{
	int ret;
	/* zero indicates non-fatal failure, retry the call */
	while ((ret = continue_or_die(recv_full(data, len))) == 0) { }
	return ret;
}

int safe_skip_data (int len)
{
	int ret;
	/* zero indicates non-fatal failure, retry the call */
	while ((ret = continue_or_die(skip_data(len))) == 0) { }
	return ret;
}
