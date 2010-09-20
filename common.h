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

/* send 32bit length + content of buffer */
int send_data (int, void *, int);
/* recv 32bit length */
int recv_length (int, int *);
/* send 32bit length */
int send_length (int, int);

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

/* EINTR retrying macros for functions that return 0/null or -1 on failure */
#define RETRY0(a, what) do { a = what; } while (a == 0 && errno == EINTR)
#define RETRY1(a, what) do { a = what; } while (a == -1 && errno == EINTR)

#endif
