#ifndef COMMANDS__H__
#define COMMANDS__H__

// assign a handle: data = filename
#define CMD_ASSIGN	0x0001
// list directory: no data
#define CMD_LIST	0x0002
// read from file: params = offset, length; no data
#define CMD_READ	0x0003
// write to file: params = offset, length; data = data to write
#define CMD_WRITE	0x0004
// delete a file: no data
// handle is preserved (?)
#define CMD_DELETE	0x0005
// rename a file: data = new name (incl. path)
// handle is redirected to new name
#define CMD_RENAME	0x0006

#endif
