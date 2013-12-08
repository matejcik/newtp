#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "structs.h"
#include "log.h"
#include "struct_helpers.h"

int pack_params_offlen (char * const buf, struct params_offlen const * s)
{
    assert(s);
    return pack_params_offlen_p(buf, s->offset, s->length);
}

int pack_params_offlen_p (char * const buf, uint64_t const offset, uint16_t const length)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_params_offlen, offset, length);
    /* assert(size == SIZEOF_params_offlen(s)); */
    return PACK_size;
}

int unpack_params_offlen (char const * const buf, int available, struct params_offlen * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_params_offlen, &s->offset, &s->length);
    assert(size == SIZEOF_params_offlen(s) || size < 0);
    return size;
}

int pack_command (char * const buf, struct command const * s)
{
    assert(s);
    return pack_command_p(buf, s->request_id, s->extension, s->command, s->handle, s->length);
}

int pack_command_p (char * const buf, uint16_t const request_id, uint8_t const extension, uint8_t const command, uint16_t const handle, uint16_t const length)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_command, request_id, extension, command, handle, length);
    /* assert(size == SIZEOF_command(s)); */
    return PACK_size;
}

int unpack_command (char const * const buf, int available, struct command * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_command, &s->request_id, &s->extension, &s->command, &s->handle, &s->length);
    assert(size == SIZEOF_command(s) || size < 0);
    return size;
}

int pack_reply (char * const buf, struct reply const * s)
{
    assert(s);
    return pack_reply_p(buf, s->request_id, s->extension, s->result, s->length);
}

int pack_reply_p (char * const buf, uint16_t const request_id, uint8_t const extension, uint8_t const result, uint16_t const length)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_reply, request_id, extension, result, length);
    /* assert(size == SIZEOF_reply(s)); */
    return PACK_size;
}

int unpack_reply (char const * const buf, int available, struct reply * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_reply, &s->request_id, &s->extension, &s->result, &s->length);
    assert(size == SIZEOF_reply(s) || size < 0);
    return size;
}

int pack_intro (char * const buf, struct intro const * s)
{
    assert(s);
    return pack_intro_p(buf, s->max_handles, s->max_opendirs, s->platform_len, s->platform, s->authstr_len, s->authstr, s->num_extensions);
}

int pack_intro_p (char * const buf, uint16_t const max_handles, uint16_t const max_opendirs, uint16_t const platform_len, char * const platform, uint16_t const authstr_len, char * const authstr, uint16_t const num_extensions)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_intro, max_handles, max_opendirs, platform_len, platform, authstr_len, authstr, num_extensions);
    /* assert(size == SIZEOF_intro(s)); */
    return PACK_size;
}

int unpack_intro (char const * const buf, int available, struct intro * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_intro, &s->max_handles, &s->max_opendirs, &s->platform_len, &s->platform, &s->authstr_len, &s->authstr, &s->num_extensions);
    assert(size == SIZEOF_intro(s) || size < 0);
    return size;
}

int pack_extension (char * const buf, struct extension const * s)
{
    assert(s);
    return pack_extension_p(buf, s->code, s->name_len, s->name);
}

int pack_extension_p (char * const buf, uint8_t const code, uint16_t const name_len, char * const name)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_extension, code, name_len, name);
    /* assert(size == SIZEOF_extension(s)); */
    return PACK_size;
}

int unpack_extension (char const * const buf, int available, struct extension * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_extension, &s->code, &s->name_len, &s->name);
    assert(size == SIZEOF_extension(s) || size < 0);
    return size;
}

int pack_auth_initial (char * const buf, struct auth_initial const * s)
{
    assert(s);
    return pack_auth_initial_p(buf, s->mechanism_len, s->mechanism, s->response_len, s->response);
}

int pack_auth_initial_p (char * const buf, uint16_t const mechanism_len, char * const mechanism, uint16_t const response_len, char * const response)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_auth_initial, mechanism_len, mechanism, response_len, response);
    /* assert(size == SIZEOF_auth_initial(s)); */
    return PACK_size;
}

int unpack_auth_initial (char const * const buf, int available, struct auth_initial * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_auth_initial, &s->mechanism_len, &s->mechanism, &s->response_len, &s->response);
    assert(size == SIZEOF_auth_initial(s) || size < 0);
    return size;
}

int pack_auth_outcome (char * const buf, struct auth_outcome const * s)
{
    assert(s);
    return pack_auth_outcome_p(buf, s->result, s->adata_len, s->adata);
}

int pack_auth_outcome_p (char * const buf, uint8_t const result, uint16_t const adata_len, char * const adata)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_auth_outcome, result, adata_len, adata);
    /* assert(size == SIZEOF_auth_outcome(s)); */
    return PACK_size;
}

int unpack_auth_outcome (char const * const buf, int available, struct auth_outcome * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_auth_outcome, &s->result, &s->adata_len, &s->adata);
    assert(size == SIZEOF_auth_outcome(s) || size < 0);
    return size;
}

int pack_dir_entry (char * const buf, struct dir_entry const * s)
{
    assert(s);
    return pack_dir_entry_p(buf, s->name_len, s->name, s->attr_len, s->attr);
}

int pack_dir_entry_p (char * const buf, uint16_t const name_len, char * const name, uint16_t const attr_len, char * const attr)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_dir_entry, name_len, name, attr_len, attr);
    /* assert(size == SIZEOF_dir_entry(s)); */
    return PACK_size;
}

int unpack_dir_entry (char const * const buf, int available, struct dir_entry * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_dir_entry, &s->name_len, &s->name, &s->attr_len, &s->attr);
    assert(size == SIZEOF_dir_entry(s) || size < 0);
    return size;
}

int pack_statvfs_result (char * const buf, struct statvfs_result const * s)
{
    assert(s);
    return pack_statvfs_result_p(buf, s->device_id, s->capacity, s->free_space, s->readonly);
}

int pack_statvfs_result_p (char * const buf, uint32_t const device_id, uint64_t const capacity, uint64_t const free_space, uint8_t const readonly)
{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_statvfs_result, device_id, capacity, free_space, readonly);
    /* assert(size == SIZEOF_statvfs_result(s)); */
    return PACK_size;
}

int unpack_statvfs_result (char const * const buf, int available, struct statvfs_result * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, available, FORMAT_statvfs_result, &s->device_id, &s->capacity, &s->free_space, &s->readonly);
    assert(size == SIZEOF_statvfs_result(s) || size < 0);
    return size;
}

