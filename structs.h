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
	uint16_t length;
};

struct command {
	uint16_t request_id;	/* return identifier */
	uint8_t  extension;
	uint8_t  command;
	uint16_t handle;	/* file resource handle */
	uint16_t length;
};

struct reply {
	uint16_t request_id;	/* same as in command */
	uint8_t  extension;
	uint8_t  result;
	uint16_t length;
};

struct intro {
	uint16_t max_handles;
	uint16_t max_opendirs;
	uint16_t platform_len;
	char *   platform;
	uint16_t authstr_len;
	char *   authstr;
	uint16_t num_extensions;
};

struct extension {
	uint8_t  code;
	uint16_t name_len;
	char *   name;
};

struct auth_initial {
	uint16_t mechanism_len;
	char *   mechanism;
	uint16_t response_len;
	char *   response;
};

struct auth_outcome {
	uint8_t  result;
	uint16_t adata_len;
	char *   adata;
};

struct dir_entry {
	uint16_t name_len;
	char *   name;
	uint16_t attr_len;
	char *   attr;
};

struct statvfs_result {
	uint32_t device_id;
	uint64_t capacity;
	uint64_t free_space;
	uint8_t  readonly;
};

#include "struct_helpers.h"

#endif
