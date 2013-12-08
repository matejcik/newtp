#ifndef STRUCT_HELPERS__H__
#define STRUCT_HELPERS__H__

#include "structs.h"

#define FORMAT_params_offlen "ls"
#define SIZEOF_params_offlen(s) (sizeof(uint64_t) + sizeof(uint16_t))
int pack_params_offlen (char * const buf, struct params_offlen const * s);
int pack_params_offlen_p (char * const buf, uint64_t const, uint16_t const);
int unpack_params_offlen (char const * const buf, int available, struct params_offlen * s);

#define FORMAT_command "sccss"
#define SIZEOF_command(s) (sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t))
int pack_command (char * const buf, struct command const * s);
int pack_command_p (char * const buf, uint16_t const, uint8_t const, uint8_t const, uint16_t const, uint16_t const);
int unpack_command (char const * const buf, int available, struct command * s);

#define FORMAT_reply "sccs"
#define SIZEOF_reply(s) (sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t))
int pack_reply (char * const buf, struct reply const * s);
int pack_reply_p (char * const buf, uint16_t const, uint8_t const, uint8_t const, uint16_t const);
int unpack_reply (char const * const buf, int available, struct reply * s);

#define FORMAT_intro "sssBsBs"
#define SIZEOF_intro(s) (sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + (s)->platform_len + sizeof(uint16_t) + (s)->authstr_len + sizeof(uint16_t))
int pack_intro (char * const buf, struct intro const * s);
int pack_intro_p (char * const buf, uint16_t const, uint16_t const, uint16_t const, char * const, uint16_t const, char * const, uint16_t const);
int unpack_intro (char const * const buf, int available, struct intro * s);

#define FORMAT_extension "csB"
#define SIZEOF_extension(s) (sizeof(uint8_t) + sizeof(uint16_t) + (s)->name_len)
int pack_extension (char * const buf, struct extension const * s);
int pack_extension_p (char * const buf, uint8_t const, uint16_t const, char * const);
int unpack_extension (char const * const buf, int available, struct extension * s);

#define FORMAT_auth_initial "sBsB"
#define SIZEOF_auth_initial(s) (sizeof(uint16_t) + (s)->mechanism_len + sizeof(uint16_t) + (s)->response_len)
int pack_auth_initial (char * const buf, struct auth_initial const * s);
int pack_auth_initial_p (char * const buf, uint16_t const, char * const, uint16_t const, char * const);
int unpack_auth_initial (char const * const buf, int available, struct auth_initial * s);

#define FORMAT_auth_outcome "csB"
#define SIZEOF_auth_outcome(s) (sizeof(uint8_t) + sizeof(uint16_t) + (s)->adata_len)
int pack_auth_outcome (char * const buf, struct auth_outcome const * s);
int pack_auth_outcome_p (char * const buf, uint8_t const, uint16_t const, char * const);
int unpack_auth_outcome (char const * const buf, int available, struct auth_outcome * s);

#define FORMAT_dir_entry "sBsB"
#define SIZEOF_dir_entry(s) (sizeof(uint16_t) + (s)->name_len + sizeof(uint16_t) + (s)->attr_len)
int pack_dir_entry (char * const buf, struct dir_entry const * s);
int pack_dir_entry_p (char * const buf, uint16_t const, char * const, uint16_t const, char * const);
int unpack_dir_entry (char const * const buf, int available, struct dir_entry * s);

#define FORMAT_statvfs_result "illc"
#define SIZEOF_statvfs_result(s) (sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint8_t))
int pack_statvfs_result (char * const buf, struct statvfs_result const * s);
int pack_statvfs_result_p (char * const buf, uint32_t const, uint64_t const, uint64_t const, uint8_t const);
int unpack_statvfs_result (char const * const buf, int available, struct statvfs_result * s);

#endif /* STRUCT_HELPERS__H__ */
