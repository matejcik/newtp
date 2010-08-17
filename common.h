#ifndef COMMON_H__
#define COMMON_H__

#include <sys/types.h>
#include "structs.h"

int send_full (int, void *, int);
int recv_full (int, void *, int);

int send_data (int, void *, int);
int recv_data (int, struct data_packet *);

#define SAFE(x) { \
	int HANDLEret = x; \
	if (HANDLEret == -1) { \
		errp("in ##x : %s", strerror(errno)); \
		die(); \
	} else if (HANDLEret == 0) { \
		log("connection lost"); \
		die(); \
	} \
}

#endif
