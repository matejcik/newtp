#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

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
			case 'Z': /* null-terminated string */
				string = va_arg(ap, char *);
				len = strlen(string);
				v16bit = htons(len);
				memcpy(buf, &v16bit, 2);
				buf += 2;
				memcpy(buf, string, len);
				buf += len;
				break;
			case 'B': /* byte string, length specified by previous short */
				string = va_arg(ap, char *);
				assert(format[-1] == 's');
				memcpy(buf, string, len);
				buf += len;
				break;
		}
		++format;
	}

	va_end(ap);

	return buf - buffer;
}

int unpack (char * const buffer, char const * format, ...)
{
	va_list ap;
	char * buf = buffer;
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
				v8bit = va_arg(ap, uint8_t *);
				*v8bit = *buf;
				buf += 1;
				break;
			case 's': /* 16bit short */
				v16bit = va_arg(ap, uint16_t *);
				memcpy(v16bit, buf, 2);
				buf += 2;
				*v16bit = ntohs(*v16bit);
				break;
			case 'i': /* 32bit int */
				v32bit = va_arg(ap, uint32_t *);
				memcpy(v32bit, buf, 4);
				buf += 4;
				*v32bit = ntohl(*v32bit);
				break;
			case 'l': /* 64bit long */
				v64bit = va_arg(ap, uint64_t *);
				memcpy(v64bit, buf, 8);
				buf += 8;
				*v64bit = ntohll(*v64bit);
				break;
			case 'Z': /* null-terminated string */
				string = va_arg(ap, char **);
				memcpy(&len, buf, 2);
				len = ntohs(len);
				buf += 2;
				*string = xmalloc(len + 1);
				memcpy(*string, buf, len);
				*string[len] = 0;
				buf += len;
				break;
			case 'B': /* byte string, length specified by previous short */
				string = va_arg(ap, char **);
				assert(format[-1] == 's');
				*string = xmalloc(*v16bit + 1);
				memcpy(*string, buf, *v16bit);
				(*string)[*v16bit] = 0;
				buf += *v16bit;
				break;
		}
		++format;
	}

	va_end(ap);

	return buf - buffer;
}

int send_full (int sock, void *data, int len)
{
	const char *buf = (const char *) data;
	int origlen = len;
	while (1) {
		int wrote = send(sock, buf, len, 0);
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

int recv_full (int sock, void *data, int len)
{
	char *buf = (char *) data;
	int orig_len = len;
	while (1) {
		int read = recv(sock, buf, len, 0);
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

int send_data (int sock, void *data, int len)
{
	uint32_t tmp = htonl(len);
	int ret;
	if ((ret = send_full(sock, &tmp, sizeof(uint32_t))) <= 0) return ret;
	if (len == 0) return sizeof(uint32_t);
	if ((ret = send_full(sock, data, len) <= 0)) return ret;
	return ret + sizeof(uint32_t);
}

int recv_data (int sock, struct data_packet * data)
{
	uint32_t tmp;
	int ret;
	if ((ret = recv_full(sock, &tmp, sizeof(uint32_t))) <= 0) return ret;
	data->len = ntohl(tmp);
	if (data->len == 0) {
		data->data = NULL;
		return sizeof(uint32_t);
	}
	data->data = xmalloc(data->len);
	if ((ret = recv_full(sock, data->data, data->len) <= 0)) {
		free(data->data);
		return ret;
	}
	return ret + sizeof(uint32_t);
}
