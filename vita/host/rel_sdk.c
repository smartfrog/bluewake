#include "boot_host.h"
#include "rel_archive.h"
#include <stdlib.h>

#define WW_REL_SDK_HEAD 0x800030C8u
#define WW_REL_SDK_TAIL 0x800030CCu
#define WW_REL_SDK_NAMES 0x800030D0u

static WwRelImage* image_at(WwHost* host, u32 guest) {
    WwRelImage* found = NULL;
    for (u32 at = 0u; at < host->rel_image_count; ++at) {
        WwRelImage* image = &host->rel_images[at];
        if (image->guest == guest && guest != 0u) {
            if (found != NULL)
                return NULL;
            found = image;
        }
    }
    return found;
}

static bool inputs_match(WwHost* host, const WwRelImage* image) {
    return mem_read32(&host->cpu, image->owner + 0x14u) == image->sdk_bss &&
        mem_read32(&host->cpu, WW_REL_SDK_NAMES) == image->sdk_name_base;
}

static const char* expected_image(WwHost* host, const WwRelImage* image, u32 next, u64* hash) {
    const BlueWakeRelInitContractV1* certificate = image->certificate;
    const StaticRecompRelModule* rel = ww_host_rel_descriptor(host, image->module_id);
    u8* live = NULL;
    if (certificate == NULL || certificate != ww_host_rel_certificate(host, image->module_id) ||
        rel == NULL || image->bytes != certificate->rel_size || image->bytes > image->capacity || !inputs_match(host, image) ||
        !ppc_guest_alias_get_storage(image->guest, image->capacity, &live))
        return "REL SDK certificate association or frozen input changed";
    const u8* raw = certificate->rel_bytes;
    u8* expected = malloc(image->bytes);
    if (expected == NULL)
        return "REL SDK reconstruction allocation failed";
    memcpy(expected, raw, image->bytes);
    const char* error = ww_rel_replay(host, rel, raw, image->bytes, expected, image, true);
    if (error == NULL) {
        write_be32(expected + 4u, next);
        write_be32(expected + 8u, image->sdk_previous);
        write_be32(expected + 0x14u, bluewake_archive_be32(raw + 0x14u) + image->sdk_name_base);
        const u32 fields[] = {0x10u, 0x24u, 0x28u, 0x48u};
        const u32 field_count = bluewake_archive_be32(raw + 0x1Cu) >= 3u ? 4u : 3u;
        for (u32 at = 0u; at < field_count; ++at)
            write_be32(expected + fields[at], image->guest + bluewake_archive_be32(raw + fields[at]));
        expected[0x33u] = 0u;
        for (u32 at = 1u; at < image->section_count; ++at) {
            const WwRelSection* section = &image->sections[at];
            if (section->flags != 0u)
                write_be32(expected + image->section_table + at * 8u, image->guest + section->flags);
            else if (section->size != 0u) {
                write_be32(expected + image->section_table + at * 8u, image->sdk_bss);
                expected[0x33u] = (u8)at;
            }
        }
        for (u32 at = 0u; at < 3u; ++at) {
            const u32 index = raw[0x30u + at];
            if (index != 0u)
                write_be32(expected + 0x34u + at * 4u, bluewake_archive_be32(raw + 0x34u + at * 4u) +
                    (bluewake_archive_be32(expected + image->section_table + index * 8u) & ~1u));
        }
        const u32 imports = bluewake_archive_be32(raw + 0x28u);
        const u32 bytes = bluewake_archive_be32(raw + 0x2Cu);
        bool trimmed = false;
        for (u32 at = imports; at < imports + bytes; at += 8u) {
            write_be32(expected + at + 4u, image->guest + bluewake_archive_be32(raw + at + 4u));
            const u32 module = bluewake_archive_be32(raw + at);
            if (image->sdk_fixed && !trimmed && (module == 0u || module == image->module_id)) {
                write_be32(expected + 0x2Cu, at - imports);
                trimmed = true;
            }
        }
        *hash = ww_host_rel_raw_hash(expected, image->bytes);
        if (memcmp(expected, live, image->bytes) != 0)
            error = "REL SDK image differs from its independently reconstructed state";
    }
    free(expected);
    return error;
}

static bool verify_list(WwHost* host, WwRelImage* candidate, u64* candidate_hash, u64* tail_hash) {
    const u32 head = host->rel_sdk_head == 0u && candidate != NULL ? candidate->guest : host->rel_sdk_head;
    const u32 tail = candidate == NULL ? host->rel_sdk_tail : candidate->guest;
    if (mem_read32(&host->cpu, WW_REL_SDK_HEAD) != head || mem_read32(&host->cpu, WW_REL_SDK_TAIL) != tail)
        return false;
    u32 members = candidate == NULL ? 0u : 1u;
    for (u32 at = 0u; at < host->rel_image_count; ++at)
        members += host->rel_images[at].sdk_phase == WW_REL_SDK_CERTIFIED ||
            host->rel_images[at].sdk_phase == WW_REL_SDK_RESIDENT;
    u32 guest = head, previous = 0u, visited = 0u;
    while (guest != 0u && visited < members) {
        WwRelImage* image = image_at(host, guest);
        if (image == NULL || ww_host_rel_live(host, image->module_id) != image)
            return false;
        const bool appended = image == candidate;
        const bool old_tail = candidate != NULL && guest == host->rel_sdk_tail;
        const u32 next = old_tail ? candidate->guest : image->sdk_next;
        if ((!appended && image->sdk_phase != WW_REL_SDK_CERTIFIED && image->sdk_phase != WW_REL_SDK_RESIDENT) ||
            image->sdk_previous != previous || (appended && next != 0u))
            return false;
        u8* live = NULL;
        if (image->bytes < 12u || !ppc_guest_alias_get_storage(guest, image->capacity, &live) ||
            bluewake_archive_be32(live) != image->module_id || bluewake_archive_be32(live + 4u) != next ||
            bluewake_archive_be32(live + 8u) != previous)
            return false;
        if (image->sdk_phase != WW_REL_SDK_RESIDENT) {
            if (appended || old_tail) {
                u64 hash = 0u;
                if (expected_image(host, image, next, &hash) != NULL)
                    return false;
                if (appended)
                    *candidate_hash = hash;
                else
                    *tail_hash = hash;
            } else if (!inputs_match(host, image) || !ww_host_rel_raw_unchanged(image)) {
                return false;
            }
        } else if (image->module_id != 1u) {
            return false;
        }
        previous = guest;
        guest = next;
        visited++;
    }
    return guest == 0u && previous == tail && visited == members;
}

bool ww_host_rel_sdk_integrity(WwHost* host) {
    u64 candidate_hash = 0u, tail_hash = 0u;
    return host != NULL && host->rel_sdk_seeded && !host->rel_sdk_failed &&
        host->rel_sdk_pending_owner == 0u && verify_list(host, NULL, &candidate_hash, &tail_hash);
}

const char* ww_host_rel_sdk_prepare(WwHost* host, WwRelImage* image, bool fixed) {
    if (host == NULL || image == NULL || host->rel_sdk_failed || image->certificate == NULL ||
        image->bytes != image->certificate->rel_size || image->bytes > image->capacity ||
        image != ww_host_rel_owned(host, image->owner) || image != ww_host_rel_live(host, image->module_id) ||
        image->certificate != ww_host_rel_certificate(host, image->module_id))
        return "REL SDK preparation requires its exact admitted live image";
    u8* live = NULL;
    if (!ppc_guest_alias_get_storage(image->guest, image->capacity, &live))
        return "REL SDK preparation lost its owned raw storage";
    if (host->rel_sdk_pending_owner != 0u)
        return host->rel_sdk_pending_owner == image->owner && image->sdk_phase == WW_REL_SDK_PENDING &&
            image->sdk_fixed == fixed && inputs_match(host, image) &&
            memcmp(live, image->certificate->rel_bytes, image->bytes) == 0 ?
            NULL : "REL SDK supports only one unchanged pending append";
    if (image->sdk_phase != WW_REL_SDK_NONE || !ww_host_rel_raw_unchanged(image) ||
        memcmp(live, image->certificate->rel_bytes, image->bytes) != 0)
        return "REL SDK preparation requires the certified original raw image";
    const u8* raw = image->certificate->rel_bytes;
    const u32 bss = mem_read32(&host->cpu, image->owner + 0x14u);
    const u32 bss_size = bluewake_archive_be32(raw + 0x20u);
    const u32 alignment = bluewake_archive_be32(raw + 0x44u);
    const u32 image_alignment = bluewake_archive_be32(raw + 0x40u);
    const u32 name_base = mem_read32(&host->cpu, WW_REL_SDK_NAMES);
    const u64 name = (u64)name_base + bluewake_archive_be32(raw + 0x14u);
    const u32 name_size = bluewake_archive_be32(raw + 0x18u);
    if ((image_alignment != 0u && image->guest % image_alignment != 0u) ||
        (bss_size == 0u && bss != 0u) || (bss_size != 0u &&
        ((bss & 3u) != 0u || bss < GC_RAM_BASE || (u64)bss + bss_size > GC_RAM_BASE + GC_MAIN_RAM_SIZE ||
         (alignment != 0u && bss % alignment != 0u) || ww_host_linked_alias_overlaps(bss, bss_size))) ||
        name > UINT32_MAX || (name != 0u && (name < GC_RAM_BASE || name >= GC_RAM_BASE + GC_MAIN_RAM_SIZE)) ||
        (name_size != 0u && (name < GC_RAM_BASE ||
        name + name_size > GC_RAM_BASE + GC_MAIN_RAM_SIZE || ww_host_linked_alias_overlaps((u32)name, name_size))))
        return "REL SDK name or BSS input is not a validated bounded MEM1 range";
    WwRelImage* profile = NULL;
    if (!host->rel_sdk_seeded) {
        const u32 head = mem_read32(&host->cpu, WW_REL_SDK_HEAD);
        const u32 tail = mem_read32(&host->cpu, WW_REL_SDK_TAIL);
        for (u32 at = 0u; at < host->rel_image_count; ++at)
            if (host->rel_images[at].module_id == 1u && host->rel_images[at].linked)
                profile = &host->rel_images[at];
        if (head == 0u && tail == 0u && profile != NULL)
            return "REL SDK empty seed would skip the resident profile";
        if (head != 0u || tail != 0u) {
            u8* resident = NULL;
            if (profile == NULL || ww_host_rel_live(host, 1u) != profile ||
                head != profile->guest || tail != profile->guest || profile->bytes < 12u ||
                !ppc_guest_alias_get_storage(profile->guest, profile->capacity, &resident) ||
                bluewake_archive_be32(resident) != 1u || bluewake_archive_be32(resident + 4u) != 0u ||
                bluewake_archive_be32(resident + 8u) != 0u)
                return "REL SDK seed requires unchanged resident profile authority and singleton list";
        }
    } else if (!ww_host_rel_sdk_integrity(host)) {
        return "REL SDK existing list authority changed before append";
    }
    if (profile != NULL) {
        profile->sdk_phase = WW_REL_SDK_RESIDENT;
        host->rel_sdk_head = profile->guest;
        host->rel_sdk_tail = profile->guest;
    }
    host->rel_sdk_seeded = true;
    image->sdk_name_base = name_base;
    image->sdk_bss = bss;
    image->sdk_fixed = fixed;
    image->sdk_previous = host->rel_sdk_tail;
    image->sdk_next = 0u;
    image->sdk_phase = WW_REL_SDK_PENDING;
    host->rel_sdk_pending_owner = image->owner;
    return NULL;
}

const char* ww_host_rel_sdk_finish(WwHost* host, WwRelImage* image) {
    if (host == NULL || image == NULL || host->rel_sdk_failed ||
        ww_host_rel_owned(host, image->owner) != image)
        return "REL SDK certification requires an intact pending authority";
    if (image->sdk_phase == WW_REL_SDK_CERTIFIED)
        return ww_host_rel_sdk_integrity(host) ? NULL : "REL SDK certified list integrity changed";
    if (host->rel_sdk_pending_owner != image->owner || image->sdk_phase != WW_REL_SDK_PENDING)
        return "REL SDK certification requires its prepared append";
    u64 candidate_hash = 0u, tail_hash = 0u;
    if (!verify_list(host, image, &candidate_hash, &tail_hash)) {
        host->rel_sdk_failed = true;
        image->sdk_phase = WW_REL_SDK_FAILED;
        return "REL SDK complete image, neighbors or publications disagree with expected append";
    }
    WwRelImage* tail = image_at(host, host->rel_sdk_tail);
    if (tail != NULL) {
        tail->sdk_next = image->guest;
        if (tail->sdk_phase == WW_REL_SDK_CERTIFIED)
            tail->raw_hash = tail_hash;
    }
    if (host->rel_sdk_head == 0u)
        host->rel_sdk_head = image->guest;
    host->rel_sdk_tail = image->guest;
    image->raw_hash = candidate_hash;
    image->sdk_phase = WW_REL_SDK_CERTIFIED;
    host->rel_sdk_pending_owner = 0u;
    return NULL;
}

const char* ww_host_rel_sdk_observe(WwHost* host, u32 address) {
    if (address != 0x80305448u && address != 0x80305424u)
        return NULL;
    WwRelImage* image = image_at(host, host->cpu.gpr[3]);
    if (image == NULL || image->certificate == NULL)
        return NULL;
    if (!ww_host_module_contains_address(host->module, address) ||
        (host->cpu.pc & ~0x40000000u) != address)
        return "REL SDK observation requires its owned translated entry PC";
    if (host->cpu.gpr[4] != mem_read32(&host->cpu, image->owner + 0x14u))
        return "REL SDK observed BSS argument disagrees with its owner";
    return ww_host_rel_sdk_prepare(host, image, address == 0x80305448u);
}
