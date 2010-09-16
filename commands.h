#ifndef COMMANDS__H__
#define COMMANDS__H__

#define CMD_NOOP	0x0000
// assign a handle: data = filename
#define CMD_ASSIGN	0x0001
// list directory: no data
#define CMD_LIST	0x0002
// continue listing
#define CMD_LIST_CONT	0x0003
// read from file: params = offset, length; no data
#define CMD_READ	0x0004
// write to file: params = offset, length; data = data to write
#define CMD_WRITE	0x0005
// delete a file: no data
// handle is preserved (?)
#define CMD_DELETE	0x0006
// rename a file: data = new name (incl. path)
// handle is redirected to new name
#define CMD_RENAME	0x0007

// cancel running operation
#define CMD_CANCEL	0x0010


#define STAT_OK 	0x01
#define STAT_CONT	0x02
#define STAT_ERROR	0x80
#define STAT_NOTFOUND	0x81 // file under this handle doesn't exist
#define STAT_NOTFILE	0x82 // not a file
#define STAT_NOTDIR	0x83 // not a directory
#define STAT_EACCESS	0x84 // permission denied
#define STAT_BADPATH	0x85 // specified path is invalid
#define STAT_BADHANDLE	0x86 // invalid handle
#define STAT_NOCONTINUE 0x87 // attempt to continue listing that was not started
#define STAT_IO		0x88 // I/O error on server
#define STAT_SERVFAIL	0x8f // internal server error
#define STAT_BADCMD	0xff // invalid command

#endif