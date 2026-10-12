#include "boot_host.h"
#include "dvd_io.h"
#include "rel_archive.h"
#include "gxruntime/dvd.h"
#include <ctype.h>
#include <stdlib.h>

static bool member_matches(const char* member, const char* name) {
    while (*name != '\0') {
        if (tolower((unsigned char)*member) != tolower((unsigned char)*name))
            return false;
        ++member;
        ++name;
    }
    return strcmp(member, ".rel") == 0;
}

const char* ww_host_rel_image(WwHost* host, const char* name, u8** image, u32* bytes) {
    if (host == NULL || name == NULL || image == NULL || bytes == NULL)
        return "REL provider requires a host, module name and output storage";
    *image = NULL;
    *bytes = 0u;
    const char* error = ww_host_disc_ready(host);
    if (error != NULL)
        return error;
    u32 start, length;
    if (!dvd_entry_info(dvd_path_to_entrynum("/RELS.arc"), &start, &length) ||
        length < 0x40u || length > 16u * 1024u * 1024u || (u64)start + length > 0x80000000ull)
        return "REL provider requires a bounded RELS.arc in the owned disc FST";
    u8* archive = malloc(length);
    if (archive == NULL)
        return "REL archive allocation failed";
    FILE* stream = bw_dvd_fopen(BW_DVD_INSTALL_PATH, "rb");
    if (stream == NULL) {
        free(archive);
        return "REL archive ISO stream open failed";
    }
    if (bw_dvd_fseeko(stream, start, SEEK_SET) != 0 || bw_dvd_fread(archive, 1u, length, stream) != length)
        error = "REL archive ISO read failed or was short";
    if (bw_dvd_fclose(stream) != 0 && error == NULL)
        error = "REL archive ISO stream close failed";
    size_t archive_bytes = length;
    if (error == NULL && memcmp(archive, "Yaz0", 4u) == 0) {
        u8* decoded = NULL;
        if (length >= 16u && bluewake_archive_be32(archive + 4u) <= 16u * 1024u * 1024u)
            decoded = yaz0_decode(archive, length, &archive_bytes);
        free(archive);
        archive = decoded;
        if (archive == NULL)
            error = "REL archive Yaz0 wrapper is invalid or exceeds the bound";
    }
    BluewakeRelArchive view;
    if (error == NULL && bluewake_rel_archive_init(&view, archive, archive_bytes) != 0)
        error = "REL archive RARC tables are invalid";
    const u8* found = NULL;
    size_t found_bytes = 0u;
    if (error == NULL) {
        for (u32 index = 0u; index < view.count; ++index) {
            const char* member_name;
            const u8* member;
            size_t size;
            const int status = bluewake_rel_archive_member(&view, index, &member_name, &member, &size);
            if (status < 0) {
                error = "REL archive member range or string is invalid";
                break;
            }
            if (status == 0 || !member_matches(member_name, name))
                continue;
            if (found != NULL) {
                error = "REL archive contains an ambiguous module name";
                break;
            }
            found = member;
            found_bytes = size;
        }
        if (error == NULL && found == NULL)
            error = "requested REL is absent from the owned ISO archive";
    }
    size_t result_bytes = found_bytes;
    if (error == NULL) {
        if (found_bytes >= 4u && memcmp(found, "Yaz0", 4u) == 0) {
            if (found_bytes >= 16u && bluewake_archive_be32(found + 4u) <= 0x100000u)
                *image = yaz0_decode(found, found_bytes, &result_bytes);
        } else if (found_bytes >= 0x4Cu && found_bytes <= 0x100000u) {
            *image = malloc(found_bytes);
            if (*image != NULL)
                memcpy(*image, found, found_bytes);
        }
        if (*image == NULL || result_bytes < 0x4Cu || result_bytes > 0x100000u)
            error = "REL member is invalid, too large or could not be allocated";
    }
    free(archive);
    if (error != NULL) {
        free(*image);
        *image = NULL;
        return error;
    }
    *bytes = (u32)result_bytes;
    return NULL;
}
