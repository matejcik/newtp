#ifndef COMMANDS__H__
#define COMMANDS__H__

#define CMD_ASSIGN	0x00
#define CMD_STAT	0x01
#define CMD_SETATTR	0x02
#define CMD_STATVFS	0x03

#define CMD_READ	0x10
#define CMD_WRITE	0x11
#define CMD_TRUNCATE	0x12

#define CMD_DELETE	0x20
#define CMD_RENAME	0x21
#define CMD_MKDIR	0x22

#define CMD_REWINDDIR	0x30
#define CMD_READDIR	0x31

#define STAT_OK		0x00 // Success

#define ERR_BADPACKET		0x80 // Malformed request packet
#define ERR_BADEXTENSION	0x81 // Unsupported extension
#define ERR_BADCOMMAND		0x82 // Unrecognized command
#define ERR_BADHANDLE		0x83 // File handle out of range
#define ERR_BADPATH		0x84 // Path string is not acceptable
#define ERR_DENIED		0x85 // Access denied
#define ERR_BUSY		0x86 // Cannot access, try again later
#define ERR_IO			0x87 // I/O error on server
#define ERR_NOTFOUND		0x88 // File not found or path has non-dir component
#define ERR_NOTDIR		0x89 // Directory operation attempted on non-directory
#define ERR_NOTFILE		0x8A // File operation attempted on directory

#define ERR_BADATTR		0x90 // Unsupported attribute code
#define ERR_CANNOTSET		0x91 // Specified attribute is not writable
#define ERR_BADVALUE		0x92 // Specified attribute value is not acceptable

#define ERR_BADOFFSET		0xA0 // Read or write offset is not acceptable
#define ERR_TOOBIG		0xA1 // File size limit reached
#define ERR_DEVFULL		0xA2 // No space left on filesystem

#define ERR_NOTEMPTY		0xB0 // Attempting to delete a non-empty directory
#define ERR_BADMOVE		0xB1 // Bad move
#define ERR_CROSSDEV		0xB2 // Move crosses filesystem boundary
#define ERR_EXISTS		0xB3 // Directory already exists
#define ERR_READDIR		0xB4 // Directory is not rewound for reading

#define ERR_FAIL		0xFE // Generic command failure
#define ERR_SERVFAIL		0xFF // Internal server error

#define MAX_LENGTH 0xffff

#endif
