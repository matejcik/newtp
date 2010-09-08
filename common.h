#ifndef COMMON_H__
#define COMMON_H__

#include <sys/types.h>
#include "structs.h"

uint64_t htonll (uint64_t);
uint64_t ntohll (uint64_t);

int pack (char * const, char const *, ...);
int unpack (char * const, char const *, ...);

int send_full (int, void *, int);
int recv_full (int, void *, int);

int send_data (int, void *, int);
int recv_data (int, struct data_packet *);

#define SAFE(x) { \
	int HANDLEret = x; \
	if (HANDLEret == -1) { \
		errp("in " #x ": %s", strerror(errno)); \
		exit(1); \
	} else if (HANDLEret == 0) { \
		log("connection lost"); \
		exit(2); \
	} \
}

#endif
