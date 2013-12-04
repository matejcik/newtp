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
int unpack (char const * const, int, char const *, ...);

void newtp_gnutls_init (int socket, int flag);
void newtp_gnutls_disconnect (int bye);

/* send()/recv() wrappers that don't return until the whole
   buffer is transferred */
int send_full (void *, int);
int recv_full (void *, int);
/* discard len bytes from input */
int skip_data (int);

/* wrappers that log errors and cleanly exit the program */
int safe_send_full (void *, int);
int safe_recv_full (void *, int);
int safe_skip_data (int);

/* check whether an error condition is safe to continue */
int continue_or_die (int);

/* EINTR retrying macros for functions that return 0/null or -1 on failure */
#define RETRY0(a, what) do { a = what; } while (a == 0 && errno == EINTR)
#define RETRY1(a, what) do { a = what; } while (a == -1 && errno == EINTR)

#endif
