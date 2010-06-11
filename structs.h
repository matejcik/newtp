#ifndef STRUCTS__H__
#define STRUCTS__H__

#include <stdint.h>

struct params_offlen {
	uint64_t offset;
	uint32_t length;
};

struct cmd_payload {
	uint16_t id;		// return identifier
	// TODO: 16bits?
	uint16_t command;	// command
	// TODO: 16bits?

	uint16_t handle;	// file resource handle
};

struct data_packet {
	uint32_t len;
	char[] data;
};

struct reply_payload {
	uint16_t	id;		// same as in command
	uint8_t		status;
	struct data_packet data;
};

struct intro {
	uint16_t	version;
	uint16_t	maxhandles;
	uint16_t	handlelen;
};

#endif
