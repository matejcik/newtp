#ifndef TOOLS_H__
#define	TOOLS_H__

/* same as malloc, except dies when fails.
oh, and zeroes out the memory */
void * xmalloc (size_t);
/* same as realloc, except dies when fails */
void * xrealloc (void *, size_t);
/* same as strncpy, except sets dest[n] to zero */
char * strncpyz (char *, char const *, size_t);

#endif

