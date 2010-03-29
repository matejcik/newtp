#ifndef STRUCTS__H__
#define STRUCTS__H__

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

struct packet

#endif
