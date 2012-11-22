#ifndef COMMANDS__H__
#define COMMANDS__H__

#define CMD_PING	0x0000
#define CMD_HELLO	0x0001
// assign a handle: data = filename
#define CMD_ASSIGN	0x0002
#define CMD_STAT	0x0003
#define CMD_SEEK_READ	0x0004
#define CMD_READ	0x0005
#define CMD_SEEK_WRITE	0x0006
#define CMD_WRITE	0x0007
#define CMD_APPEND	0x0008
// delete a file: no data
// handle is preserved (?)
#define CMD_DELETE	0x0009
// rename a file: data = new name (incl. path)
// handle is redirected to new name
#define CMD_RENAME	0x000A
#define CMD_SETATTR	0x000B
#define CMD_REWINDDIR	0x000C
#define CMD_READDIR	0x000D
#define CMD_MKDIR	0x000E

// cancel running operation - not implemented now
#define CMD_CANCEL	0x0010


#define STAT_OK 	0x01
#define STAT_ERROR	0x80
#define STAT_NOTFOUND	0x81 // file under this handle doesn't exist
#define STAT_NOTFILE	0x82 // not a file
#define STAT_NOTDIR	0x83 // not a directory
#define STAT_DENIED	0x84 // permission denied
#define STAT_BADPATH	0x85 // specified path is invalid
#define STAT_BADHANDLE	0x86 // invalid handle
#define STAT_IO		0x88 // I/O error on server
#define STAT_FULL	0x89 // underlying device is full
#define STAT_SERVFAIL	0x8f // internal server error
#define STAT_BADCMD	0xff // invalid command

#endif
