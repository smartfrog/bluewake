#include "boot_host.h"
#include "rel_archive.h"
#include "rel_scratch_allocator.h"
#include "dvd_io.h"
#include <stdlib.h>

static const char* validate_rel(WwHost* host, const u8* image, u32 bytes, u32* module_id) {
    const u32 id = bluewake_archive_be32(image);
    const u32 sections = bluewake_archive_be32(image + 0xCu);
    const u32 table = bluewake_archive_be32(image + 0x10u);
    const u32 version = bluewake_archive_be32(image + 0x1Cu);
    const u32 imports = bluewake_archive_be32(image + 0x28u);
    const u32 import_bytes = bluewake_archive_be32(image + 0x2Cu);
    if (id == 0u || version == 0u || version > 3u || sections == 0u || sections > 256u ||
        (u64)table + (u64)sections * 8u > bytes || (u64)imports + import_bytes > bytes || (import_bytes & 7u) != 0u)
        return "REL header or import/section table is invalid";
    for (u32 index = 0u; index < sections; ++index) {
        const u8* section = image + table + index * 8u;
        const u32 start = bluewake_archive_be32(section) & ~1u;
        const u32 size = bluewake_archive_be32(section + 4u);
        if (start != 0u && (u64)start + size > bytes)
            return "REL section exceeds its verified raw image";
    }
    for (u32 index = 0u; index < host->module->num_rel_modules; ++index) {
        const StaticRecompRelModule* rel = &host->module->rel_modules[index];
        if (rel->module_id == id) {
            // The immutable composite producer names the +0x04 next-link word "version".
            const u32 catalog_version_word = bluewake_archive_be32(image + 4u);
            if (rel->file_size != bytes || rel->version != catalog_version_word || rel->section_count != sections ||
                rel->section_info_offset != table)
                return "REL raw image metadata disagrees with the compiled descriptor";
            *module_id = id;
            return NULL;
        }
    }
    return "REL module identity is not owned by the compiled descriptor";
}

static const char* scratch_address(WwHost* host, u32 bytes, u32* guest) {
    size_t count = host->module->num_code_ranges;
    if (count > 16384u)
        return "REL descriptor occupied-range count exceeds its bound";
    for (u32 index = 0u; index < host->module->num_rel_modules; ++index) {
        const u32 sections = host->module->rel_modules[index].num_sections;
        if (sections > 16384u || count > 16384u - sections)
            return "REL descriptor occupied-range count exceeds its bound";
        count += sections;
    }
    BlueWakeScratchRange* ranges = calloc(count, sizeof(*ranges));
    if (ranges == NULL)
        return "REL scratch range allocation failed";
    size_t at = 0u;
    for (u32 index = 0u; index < host->module->num_code_ranges; ++index) {
        const StaticRecompRange range = host->module->code_ranges[index];
        if (range.end < range.start) {
            free(ranges);
            return "REL scratch descriptor contains a reversed code range";
        }
        ranges[at++] = (BlueWakeScratchRange){range.start & ~0x40000000u, range.end - range.start};
    }
    for (u32 index = 0u; index < host->module->num_rel_modules; ++index) {
        const StaticRecompRelModule* rel = &host->module->rel_modules[index];
        for (u32 section = 0u; section < rel->num_sections; ++section) {
            const StaticRecompRelSection* linked = &rel->sections[section];
            ranges[at++] = (BlueWakeScratchRange){linked->linked_start & ~0x40000000u, linked->size};
        }
    }
    const bool found = bluewake_rel_scratch_first_fit(WW_REL_SCRATCH_BASE, WW_REL_SCRATCH_LIMIT,
                                                    bytes, ranges, count, guest);
    free(ranges);
    if (!found)
        return "REL scratch has no bounded unoccupied range";
    if (ww_host_linked_alias_overlaps(*guest, bytes) || ww_host_linked_alias_overlaps(*guest | 0x40000000u, bytes))
        return "REL scratch overlaps an existing alias owner";
    return NULL;
}

const char* ww_host_materialize_rel(WwHost* host, u32 owner, const char* name) {
    if (host == NULL)
        return "REL materialization requires an owned host";
    if (host->rel_image.guest != 0u)
        return "REL image is already materialized; linking remains unsupported";
    if (host->module == NULL || host->module->rel_modules == NULL)
        return "REL materialization requires the compiled module catalog";
    if (name == NULL || name[0] == '\0' || strlen(name) > 95u || strspn(name,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(name))
        return "REL materialization requires a bounded module identifier";
    u8* image;
    u32 bytes;
    const char* error = ww_host_rel_image(host, name, &image, &bytes);
    if (error != NULL)
        return error;
    u32 module_id = 0u, guest = 0u;
    error = validate_rel(host, image, bytes, &module_id);
    const u32 capacity = (bytes + 31u) & ~31u;
    if (error == NULL)
        error = scratch_address(host, capacity, &guest);
    if (error == NULL && !ppc_guest_alias_add(guest, capacity, NULL))
        error = "REL scratch alias allocation failed";
    if (error == NULL) {
        u8* storage = NULL;
        if (!ppc_guest_alias_get_storage(guest, capacity, &storage)) {
            error = "REL scratch alias storage is unavailable";
        } else {
            memcpy(storage, image, bytes);
            if (memcmp(storage, image, bytes) != 0)
                error = "REL scratch copy differs from the verified raw image";
            else if (fprintf(stderr, "[rel-materialized] module=%u name=%s bytes=%u guest=%08X owner=%08X source=iso-RELS.arc backend=%s\n",
                             module_id, name, bytes, guest, owner, bw_dvd_io_backend_name()) < 0)
                error = "REL materialization trace write failed";
        }
        if (error != NULL) {
            if (!ppc_guest_alias_remove(guest, capacity))
                ww_host_fail(host, "REL failed mapping could not be removed", guest, host->cpu.pc);
        } else {
            host->rel_image = (WwRelImage){owner, guest, bytes, capacity, module_id};
            host->alias_count++;
            host->alias_bytes += capacity;
        }
    }
    free(image);
    return error;
}
