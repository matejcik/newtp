#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "common.h"
#include "structs.h"
#include "tools.h"

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
	if ((ret = send_full(sock, data, len) <= 0)) return ret;
	return ret + sizeof(uint32_t);
}

int recv_data (int sock, struct data_packet * data)
{
	uint32_t tmp;
	int ret;
	if ((ret = recv_full(sock, &tmp, sizeof(uint32_t))) <= 0) return ret;
	data->len = ntohl(tmp);
	data->data = xmalloc(data->len);
	if ((ret = send_full(sock, data->data, data->len) <= 0)) {
		free(data->data);
		return ret;
	}
	return ret + sizeof(uint32_t);
}
