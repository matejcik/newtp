#ifndef STRUCTS__H__
#define STRUCTS__H__

#include <stdint.h>

// this file is used as a source to auto-generate struct_helpers.h and .c
// Because of that, the structures defined within must keep to
// a specific syntax:
// struct structure_name {
//    type name; /* optional comment */
//    ....(one line per item)
// };
//
// run make_struct_helpers.py to regenerate. this is also part of Makefile

struct params_offlen {
	uint64_t offset;
	uint32_t length;
};

struct command {
	uint16_t id;		/* return identifier TODO 16bits?? */
	uint16_t command;	/* TODO 16bits?? */
	uint16_t handle;	/* file resource handle */
};

struct data_packet { /* this does not have an auto-generated helper */
	uint32_t len;
	char *data;
};

struct reply {
	uint16_t	id;		/* same as in command */
	uint8_t		status;
};

struct intro {
	uint16_t	version;
	uint16_t	maxhandles;
	uint16_t	handlelen;
};

/* actual values */
#define ENTRY_FILE	1
#define ENTRY_DIR	2
#define ENTRY_OTHER	0
#define ENTRY_BAD	0xff

/* bitmasks */
#define PERM_READ	0x01
#define PERM_WRITE	0x02
struct dir_entry {
	uint16_t	len;
	char *		name;
	uint8_t		type;
	uint8_t		perm;
	uint64_t	size;
};

#include "struct_helpers.h"

#endif