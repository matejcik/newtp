#ifndef COMMANDS__H__
#define COMMANDS__H__

/*** commands ***/

/* basic */
#define CMD_ASSIGN	0x00
#define CMD_STAT	0x01
#define CMD_SETATTR	0x02
#define CMD_STATVFS	0x03
/* file manipulation */
#define CMD_READ	0x10
#define CMD_WRITE	0x11
#define CMD_TRUNCATE	0x12
/* directory manipulation */
#define CMD_DELETE	0x20
#define CMD_RENAME	0x21
#define CMD_MAKEDIR	0x22
/* directory listing */
#define CMD_REWINDDIR	0x30
#define CMD_READDIR	0x31

/*** result codes ***/

/* success */
#define STAT_OK		0x00 // Success
#define STAT_CONTINUED	0x01 // Directory listing continues
#define STAT_FINISHED	0x02 // Directory listing ends
/* generic failure */
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
/* attribute errors */
#define ERR_BADATTR		0x90 // Unsupported attribute code
#define ERR_BADVALUE		0x91 // Specified attribute value is not acceptable
/* file size errors */
#define ERR_BADOFFSET		0xA0 // Read or write offset is not acceptable
#define ERR_TOOBIG		0xA1 // File size limit reached
#define ERR_DEVFULL		0xA2 // No space left on filesystem
/* directory manipulation errors */
#define ERR_NOTEMPTY		0xB0 // Attempting to delete a non-empty directory
#define ERR_BADMOVE		0xB1 // Bad move
#define ERR_CROSSDEV		0xB2 // Move crosses filesystem boundary
#define ERR_EXISTS		0xB3 // Directory already exists
#define ERR_READDIR		0xB4 // Directory is not rewound for reading
/* server errors */
#define ERR_UNSUPPORTED		0xFD // Command or argument unsupported
#define ERR_FAIL		0xFE // Generic command failure
#define ERR_SERVFAIL		0xFF // Internal server error

/*** attribute codes ***/

/* generic codes */
#define ATTR_TYPE	0x00
#define ATTR_RIGHTS	0x01
#define ATTR_SIZE	0x02
#define ATTR_DEV_ID	0x03
#define ATTR_LINKS	0x04
#define ATTR_ATIME	0x05
#define ATTR_MTIME	0x06
/* POSIX-specific codes */
#define ATTR_PTYPE	0x10
#define ATTR_PERMS	0x11
#define ATTR_CTIME	0x12
#define ATTR_UID	0x13
#define ATTR_GID	0x14

/* values of ATTR_TYPE */
#define TYPE_FILE  0x01
#define TYPE_DIR   0x02
#define TYPE_OTHER 0x00
#define TYPE_BAD   0xff
/* values of ATTR_RIGHTS */
#define RIGHTS_READ  0x01
#define RIGHTS_WRITE 0x02
/* values of ATTR_PTYPE */
#define PTYPE_FIFO    0x01
#define PTYPE_CHAR    0x02
#define PTYPE_DIR     0x04
#define PTYPE_BLOCK   0x06
#define PTYPE_FILE    0x08
#define PTYPE_SYMLINK 0x0a
#define PTYPE_SOCKET  0x0c



#define MAX_LENGTH 0xffff


#define EXT_CORE	0x00	/* core protocol features */
#define EXT_INIT	0xff	/* session init commands */

#define INIT_WELCOME	0x00

#define SASL_START     0x10
#define SASL_START_OPT 0x11
#define SASL_RESPONSE  0x12

#define SASL_R_CHALLENGE     0x10
#define SASL_R_SUCCESS       0x11
#define SASL_R_SUCCESS_OPT   0x12
#define SASL_E_FAILED        0x20
#define SASL_E_BAD_MECHANISM 0x21
#define SASL_E_BAD_MESSAGE   0x22

#endif
