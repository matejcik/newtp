#ifndef TOOLS_H
#define	TOOLS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void * xmalloc (size_t what)
{
	void* v = malloc(what);
	if (!v) {
		fprintf(stderr, "out of memory!\n");
		exit(13);
	}
	memset(v, 0, what);
	return v;
}

static void * xrealloc (void *ptr, size_t what)
{
	ptr = realloc(ptr, what);
	if (!ptr) {
		fprintf(stderr, "out of memory!\n");
		exit(13);
	}
	return ptr;
}

#endif	/* TOOLS_H */

