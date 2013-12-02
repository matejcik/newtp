#ifndef COMMON_H__
#define COMMON_H__

#include <sys/types.h>
#include "structs.h"

/* host-to-network and network-to-host byte order conversion
 for 64bit ints */
uint64_t htonll (uint64_t);
uint64_t ntohll (uint64_t);

/* binary serialization and de-serialization with sprintf-like
   format specifier */
int pack (char * const, char const *, ...);
int unpack (char const * const, char const *, ...);

/* send()/recv() wrappers that don't return until the whole
   buffer is transferred */
int send_full (int, void *, int);
int recv_full (int, void *, int);
/* discard len bytes from input */
int skip_data (int, int);

/* network call wrapper that dies on failure */
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
