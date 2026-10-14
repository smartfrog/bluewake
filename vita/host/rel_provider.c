#include "boot_host.h"
#include "dvd_io.h"
#include "rel_archive.h"
#include "gxruntime/dvd.h"
#include <ctype.h>
#include <stdio.h>
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

// One bounded per-request fallback: when the archive has no matching member,
// read the loose rels/<name>.rel file range through the same DVD seam and
// decoder the archive path uses. The raw bytes are returned untouched.
static const char* loose_rel_image(const char* name, u8** image, size_t* result_bytes) {
    if (!bluewake_rel_module_name_valid(name))
        return "REL loose module name is not a bounded identifier";
    char path[128];
    const int written = snprintf(path, sizeof(path), "/rels/%s.rel", name);
    if (written < 0 || (size_t)written >= sizeof(path))
        return "REL loose module path exceeds its bound";
    u32 start = 0u, source_bytes = 0u;
    const s32 entry = dvd_path_to_entrynum(path);
    if (entry < 0 || !dvd_entry_info(entry, &start, &source_bytes))
        return "requested REL is absent from the owned ISO archive and loose rels/";
    if (source_bytes == 0u || source_bytes > 16u * 1024u * 1024u ||
        (u64)start + source_bytes > 0x80000000ull)
        return "REL loose member ISO range is invalid or unbounded";
    u8* member = malloc(source_bytes);
    if (member == NULL)
        return "REL loose member allocation failed";
    FILE* stream = bw_dvd_fopen(BW_DVD_INSTALL_PATH, "rb");
    if (stream == NULL) {
        free(member);
        return "REL loose member ISO stream open failed";
    }
    const char* error = NULL;
    if (bw_dvd_fseeko(stream, start, SEEK_SET) != 0 ||
        bw_dvd_fread(member, 1u, source_bytes, stream) != source_bytes)
        error = "REL loose member ISO read failed or was short";
    if (bw_dvd_fclose(stream) != 0 && error == NULL)
        error = "REL loose member ISO stream close failed";
    if (error != NULL) {
        free(member);
        return error;
    }
    if (source_bytes >= 4u && memcmp(member, "Yaz0", 4u) == 0) {
        size_t decoded_bytes = source_bytes;
        u8* decoded = NULL;
        if (source_bytes >= 16u && bluewake_archive_be32(member + 4u) <= 0x100000u)
            decoded = yaz0_decode(member, source_bytes, &decoded_bytes);
        free(member);
        member = decoded;
        if (member == NULL)
            return "REL loose member Yaz0 wrapper is invalid or exceeds the bound";
        *result_bytes = decoded_bytes;
    } else {
        *result_bytes = source_bytes;
    }
    if (*result_bytes < 0x4Cu || *result_bytes > 0x100000u) {
        free(member);
        return "REL loose member is invalid, too large or could not be allocated";
    }
    *image = member;
    return NULL;
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
    }
    size_t result_bytes = found_bytes;
    if (error == NULL && found != NULL) {
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
    if (error == NULL && found == NULL)
        error = loose_rel_image(name, image, &result_bytes);
    free(archive);
    if (error != NULL) {
        free(*image);
        *image = NULL;
        return error;
    }
    *bytes = (u32)result_bytes;
    return NULL;
}
