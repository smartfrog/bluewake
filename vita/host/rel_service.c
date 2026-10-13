#include "boot_host.h"
#include "rel_archive.h"
#include "rel_scratch_allocator.h"
#include "dvd_io.h"
#include "rel_profile_list.h"
#include <stdlib.h>
#include <strings.h>

WwRelImage* ww_host_rel_owned(WwHost* host, u32 owner) {
    for (u32 index = 0u; index < WW_REL_MAX_IMAGES; ++index)
        if (host->rel_images[index].guest != 0u && host->rel_images[index].owner == owner)
            return &host->rel_images[index];
    return NULL;
}

const StaticRecompRelModule* ww_host_rel_descriptor(WwHost* host, u32 module_id) {
    for (u32 index = 0u; index < host->module->num_rel_modules; ++index)
        if (host->module->rel_modules[index].module_id == module_id)
            return &host->module->rel_modules[index];
    return NULL;
}

bool ww_host_rel_linked(WwHost* host, u32 module_id) {
    for (u32 index = 0u; index < WW_REL_MAX_IMAGES; ++index)
        if (host->rel_images[index].module_id == module_id && host->rel_images[index].linked)
            return true;
    return false;
}

const char* ww_host_collect_rels(WwHost* host) {
    for (u32 index = 0u; index < WW_REL_MAX_IMAGES; ++index) {
        WwRelImage* slot = &host->rel_images[index];
        if (slot->guest == 0u || !slot->linked)
            continue;
        const u32 published = mem_read32(&host->cpu, slot->owner + 0x10u);
        if (published == slot->guest)
            continue;
        if (published != 0u)
            return "REL owner changed to an unowned image";
        if (slot->module_id == 1u && mem_read32(&host->cpu, BLUEWAKE_PROFILE_LIST_GLOBAL) != 0u)
            return "REL profile owner was cleared without unlinking its global";
        if (!ppc_guest_alias_remove(slot->guest, slot->capacity))
            return "REL unloaded scratch mapping could not be removed";
        host->alias_count--;
        host->alias_bytes -= slot->capacity;
        *slot = (WwRelImage){0};
    }
    while (host->rel_image_count != 0u && host->rel_images[host->rel_image_count - 1u].guest == 0u)
        host->rel_image_count--;
    return NULL;
}

static const char* validate_rel(WwHost* host, const u8* image, u32 bytes, u32* module_id) {
    if (image == NULL || bytes < 0x4Cu || bytes > 0x100000u)
        return "REL raw image size is invalid";
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
            if (id != 1u) {
                const char* error = ww_host_validate_rel_link(host, rel, image, bytes);
                if (error != NULL)
                    return error;
            }
            *module_id = id;
            return NULL;
        }
    }
    return "REL module identity is not owned by the compiled descriptor";
}

static const char* scratch_address(WwHost* host, u32 bytes, u32* guest) {
    size_t count = host->module->num_code_ranges;
    if (count > 16384u - WW_REL_MAX_IMAGES)
        return "REL descriptor occupied-range count exceeds its bound";
    count += WW_REL_MAX_IMAGES;
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
    for (u32 index = 0u; index < WW_REL_MAX_IMAGES; ++index) {
        const WwRelImage* slot = &host->rel_images[index];
        ranges[at++] = (BlueWakeScratchRange){slot->guest, slot->capacity};
    }
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
    if (host->module == NULL || host->module->rel_modules == NULL)
        return "REL materialization requires the compiled module catalog";
    if (name == NULL || name[0] == '\0' || strlen(name) > 95u || strspn(name,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(name))
        return "REL materialization requires a bounded module identifier";
    if ((owner & 3u) != 0u || owner < GC_RAM_BASE || (u64)owner + 0x20u > (u64)GC_RAM_BASE + host->cpu.ram_size ||
        ww_host_linked_alias_overlaps(owner, 0x20u))
        return "REL materialization requires a real MEM1 owner";
    const char* error = ww_host_collect_rels(host);
    if (error != NULL)
        return error;
    WwRelImage* slot = ww_host_rel_owned(host, owner);
    if (slot != NULL)
        return strcasecmp(slot->name, name) == 0 ? NULL : "REL owner requested a different module while still owned";
    if (mem_read32(&host->cpu, owner + 0x10u) != 0u)
        return "REL materialization would overwrite an unowned guest module";
    for (u32 index = 0u; index < WW_REL_MAX_IMAGES && slot == NULL; ++index)
        if (host->rel_images[index].guest == 0u)
            slot = &host->rel_images[index];
    if (slot == NULL)
        return "REL owned image table is full";
    u8* image;
    u32 bytes;
    error = ww_host_rel_image(host, name, &image, &bytes);
    if (error != NULL)
        return error;
    u32 module_id = 0u, guest = 0u;
    error = validate_rel(host, image, bytes, &module_id);
    for (u32 index = 0u; error == NULL && index < WW_REL_MAX_IMAGES; ++index)
        if (host->rel_images[index].guest != 0u && host->rel_images[index].module_id == module_id)
            error = "REL module already belongs to another guest owner";
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
            *slot = (WwRelImage){.owner = owner, .guest = guest, .bytes = bytes, .capacity = capacity,
                                 .module_id = module_id};
            memcpy(slot->name, name, strlen(name) + 1u);
            const u32 count = (u32)(slot - host->rel_images) + 1u;
            if (host->rel_image_count < count)
                host->rel_image_count = count;
            host->alias_count++;
            host->alias_bytes += capacity;
        }
    }
    free(image);
    return error;
}
