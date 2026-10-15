#include "boot_host.h"
#include "rel_archive.h"
#include <stdlib.h>

#define WW_REL_SDK_HEAD 0x800030C8u
#define WW_REL_SDK_TAIL 0x800030CCu
#define WW_REL_SDK_NAMES 0x800030D0u

#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
static const char* actor_diagnostic_failed(WwHost* host, const char* clause) {
    const int size = snprintf(host->rel_actor_diagnostic_error, sizeof(host->rel_actor_diagnostic_error),
        "%s; refusal diagnostic write/flush failed", clause);
    return size >= 0 && (size_t)size < sizeof(host->rel_actor_diagnostic_error) ? host->rel_actor_diagnostic_error : clause;
}
#endif

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
    if (mem_read32(&host->cpu, WW_REL_SDK_NAMES) != image->sdk_name_base)
        return false;
    switch (image->sdk_input_form) {
    case WW_REL_SDK_OWNER_PUBLISHED:
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        if (ww_host_actor_sdk_contract(host, image->module_id) == WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO)
            return false;
#endif
        return mem_read32(&host->cpu, image->owner + 0x14u) == image->sdk_bss;
    case WW_REL_SDK_TITLE_INLINE_ZERO:
        return ww_host_rel_title_inputs(host, image);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    case WW_REL_SDK_CERTIFIED_INLINE_ZERO:
        return ww_host_rel_actor_inputs(host, image);
#endif
    default:
        return false;
    }
}

static const char* input_refusal(WwHost* host, const WwRelImage* image, bool fixed, u32 bss,
                                  u32 form, const char* clause) {
#if WW05_ENABLE_IKADA_CONTRACT
    if (image->module_id == 60u && host->rel_ikada_admission)
        return clause;
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (ww_host_actor_batch_certificate(host, image->module_id) != NULL)
        return clause;
#endif
    if (!ww_host_rel_profile_enabled(host))
        return clause;
    if (image->module_id == 400u && host->rel_title_block.input_refusal_reported)
        return clause;
    const BlueWakeRelInitContractV1* certificate = ww_host_rel_certificate(host, image->module_id);
    if (certificate == NULL || certificate != image->certificate || certificate->rel_size < 0x4Cu)
        return clause;
    const u8* raw = certificate->rel_bytes;
    const u32 name_base = mem_read32(&host->cpu, WW_REL_SDK_NAMES);
    const u64 name = (u64)name_base + bluewake_archive_be32(raw + 0x14u);
    if (image->module_id == 400u)
        host->rel_title_block.input_refusal_reported = true;
    if (fprintf(stderr, "[rel-sdk-input-refusal] module=%u fixed=%u input_form=%u owner_bss=%08X bss=%08X bss_size=%u name_base=%08X name=%016llX name_size=%u expected_bss=%08X actual_r4=%08X clause=%s\n",
        image->module_id, fixed, form, mem_read32(&host->cpu, image->owner + 0x14u), bss,
        bluewake_archive_be32(raw + 0x20u), name_base, (unsigned long long)name,
        bluewake_archive_be32(raw + 0x18u), host->rel_title_block.guest + host->rel_title_block.extent,
        host->cpu.gpr[4], clause) < 0)
        return "REL SDK name/BSS or fixed-sequence input refused; refusal diagnostic write failed";
    return clause;
}

#if WW05_ENABLE_IKADA_CONTRACT
static const char* ikada_refusal(WwHost* host, const WwRelImage* image, bool fixed, const char* clause) {
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (host == NULL || image == NULL ||
        !((image->module_id == 60u && host->rel_ikada_admission) ||
          ww_host_actor_batch_certificate(host, image->module_id) != NULL))
        return clause;
    const BlueWakeRelInitContractV1* certificate = ww_host_rel_certificate(host, image->module_id);
#else
    if (host == NULL || image == NULL || image->module_id != 60u || !host->rel_ikada_admission)
        return clause;
    const BlueWakeRelInitContractV1* certificate = ww_host_rel_certificate(host, 60u);
#endif
    const bool valid = certificate != NULL && certificate == image->certificate && certificate->rel_size >= 0x4Cu;
    const u8* raw = valid ? certificate->rel_bytes : NULL;
    const u32 name_base = mem_read32(&host->cpu, WW_REL_SDK_NAMES);
    const bool owner_bounded = image->owner >= GC_RAM_BASE &&
        (u64)image->owner + 0x18u <= GC_RAM_BASE + GC_MAIN_RAM_SIZE;
    const u32 owner_bss = owner_bounded ? mem_read32(&host->cpu, image->owner + 0x14u) : 0u;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    WwRelBlock* block = ww_host_rel_actor_block(host, image);
    if (block != NULL && block->input_refusal_reported)
        return clause;
    if (block != NULL)
        block->input_refusal_reported = true;
    if (fprintf(stderr, "[rel-sdk-input-refusal] module=%u fixed=%u input_form=%u pc=%08X caller=%08X owner=%08X base=%08X actual_r3=%08X actual_r4=%08X owner_bss=%08X owner_bounded=%u bss_size=%u bss_alignment=%u name_base=%08X name=%016llX name_size=%u expected_bss=%08X raw_fix=%u extent=%u certificate=%s policy=%s clause=%s\n",
        image->module_id,
#else
    if (fprintf(stderr, "[rel-sdk-input-refusal] module=60 fixed=%u input_form=%u pc=%08X caller=%08X owner=%08X base=%08X actual_r3=%08X actual_r4=%08X owner_bss=%08X owner_bounded=%u bss_size=%u bss_alignment=%u name_base=%08X name=%016llX name_size=%u clause=%s\n",
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        fixed, image->sdk_phase != WW_REL_SDK_NONE ? image->sdk_input_form :
            ww_host_actor_sdk_contract(host, image->module_id) == WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO ?
            WW_REL_SDK_CERTIFIED_INLINE_ZERO : WW_REL_SDK_OWNER_PUBLISHED,
        host->cpu.pc, host->cpu.lr, image->owner, image->guest,
#else
        fixed, WW_REL_SDK_OWNER_PUBLISHED, host->cpu.pc, host->cpu.lr, image->owner, image->guest,
#endif
        host->cpu.gpr[3], host->cpu.gpr[4], owner_bss, owner_bounded,
        raw == NULL ? 0u : bluewake_archive_be32(raw + 0x20u),
        raw == NULL ? 0u : bluewake_archive_be32(raw + 0x44u), name_base,
        (unsigned long long)((u64)name_base + (raw == NULL ? 0u : bluewake_archive_be32(raw + 0x14u))),
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        raw == NULL ? 0u : bluewake_archive_be32(raw + 0x18u), block == NULL ? owner_bss : block->guest + block->extent,
        block == NULL ? 0u : block->raw_fix, block == NULL ? 0u : block->extent,
        valid ? certificate->build_identity : "invalid", host->rel_actor_policy_identity == NULL ? "inactive" : host->rel_actor_policy_identity,
        clause) < 0 || fflush(stderr) != 0)
#else
        raw == NULL ? 0u : bluewake_archive_be32(raw + 0x18u), clause) < 0)
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        return actor_diagnostic_failed(host, clause);
#else
        return "REL ikada SDK input refused; refusal diagnostic write failed";
#endif
    return clause;
}

#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
static bool ikada_trace(WwHost* host, const WwRelImage* image, bool fixed, u32 bss, u32 name_base,
                        const char* stage, const char* whole_image, u32 form) {
#else
static bool ikada_trace(WwHost* host, const WwRelImage* image, bool fixed, u32 bss, u32 name_base,
                        const char* stage, const char* whole_image) {
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!((image->module_id == 60u && host->rel_ikada_admission) ||
          ww_host_actor_batch_certificate(host, image->module_id) != NULL))
#else
    if (image->module_id != 60u || !host->rel_ikada_admission)
#endif
        return true;
    const u8* raw = image->certificate->rel_bytes;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const WwRelBlock* block = ww_host_rel_actor_block(host, image);
    return fprintf(stderr, "[rel-sdk] module=%u stage=%s certificate=%s set=%s pc=%08X caller=%08X owner=%08X base=%08X actual_r3=%08X actual_r4=%08X fixed=%u input_form=%u owner_bss=%08X bss=%08X bss_size=%u bss_alignment=%u name_base=%08X name=%016llX name_size=%u previous=%08X head=%08X tail=%08X whole_image=%s raw_fix=%u extent=%u expected_bss=%08X policy=%s\n",
        image->module_id,
#else
    return fprintf(stderr, "[rel-sdk] module=60 stage=%s certificate=%s set=%s pc=%08X caller=%08X owner=%08X base=%08X actual_r3=%08X actual_r4=%08X fixed=%u input_form=%u owner_bss=%08X bss=%08X bss_size=%u bss_alignment=%u name_base=%08X name=%016llX name_size=%u previous=%08X head=%08X tail=%08X whole_image=%s\n",
#endif
        stage, image->certificate->build_identity, host->rel_contracts->build_identity,
        host->cpu.pc, host->cpu.lr, image->owner, image->guest, host->cpu.gpr[3], host->cpu.gpr[4],
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        fixed, form, mem_read32(&host->cpu, image->owner + 0x14u), bss,
#else
        fixed, WW_REL_SDK_OWNER_PUBLISHED, mem_read32(&host->cpu, image->owner + 0x14u), bss,
#endif
        bluewake_archive_be32(raw + 0x20u), bluewake_archive_be32(raw + 0x44u), name_base,
        (unsigned long long)((u64)name_base + bluewake_archive_be32(raw + 0x14u)),
        bluewake_archive_be32(raw + 0x18u), host->rel_sdk_tail,
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        mem_read32(&host->cpu, WW_REL_SDK_HEAD), mem_read32(&host->cpu, WW_REL_SDK_TAIL), whole_image,
        block == NULL ? 0u : block->raw_fix, block == NULL ? 0u : block->extent,
        block == NULL ? bss : block->guest + block->extent,
        host->rel_actor_policy_identity == NULL ? "inactive" : host->rel_actor_policy_identity) >= 0 && fflush(stderr) == 0;
#else
        mem_read32(&host->cpu, WW_REL_SDK_HEAD), mem_read32(&host->cpu, WW_REL_SDK_TAIL), whole_image) >= 0;
#endif
}
#endif

const char* ww_rel_sdk_structure(const WwRelImage* image, const u8* raw, u8* expected, u32 next) {
    write_be32(expected + 4u, next);
    write_be32(expected + 8u, image->sdk_previous);
    const u64 name = (u64)bluewake_archive_be32(raw + 0x14u) + image->sdk_name_base;
    if (name > UINT32_MAX)
        return "REL SDK name reconstruction overflow";
    write_be32(expected + 0x14u, (u32)name);
    const u32 fields[] = {0x10u, 0x24u, 0x28u, 0x48u};
    const u32 field_count = bluewake_archive_be32(raw + 0x1Cu) >= 3u ? 4u : 3u;
    for (u32 at = 0u; at < field_count; ++at) {
        const u32 offset = bluewake_archive_be32(raw + fields[at]);
        if ((fields[at] != 0x48u && offset > image->bytes) || (u64)image->guest + offset > UINT32_MAX)
            return "REL SDK header reconstruction exceeds its bounded image";
        write_be32(expected + fields[at], image->guest + offset);
    }
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
        if (index == 0u)
            continue;
        if (index >= image->section_count || bluewake_archive_be32(raw + 0x34u + at * 4u) >= image->sections[index].size)
            return "REL SDK lifecycle reconstruction exceeds its section";
        write_be32(expected + 0x34u + at * 4u, bluewake_archive_be32(raw + 0x34u + at * 4u) +
            (bluewake_archive_be32(expected + image->section_table + index * 8u) & ~1u));
    }
    const u32 imports = bluewake_archive_be32(raw + 0x28u);
    const u32 bytes = bluewake_archive_be32(raw + 0x2Cu);
    if ((u64)imports + bytes > image->bytes || (bytes & 7u) != 0u)
        return "REL SDK reconstruction import table is invalid";
    bool trimmed = false;
    for (u32 at = imports; at < imports + bytes; at += 8u) {
        const u32 stream = bluewake_archive_be32(raw + at + 4u);
        if (stream >= image->bytes)
            return "REL SDK reconstruction stream is outside its image";
        write_be32(expected + at + 4u, image->guest + stream);
        const u32 module = bluewake_archive_be32(raw + at);
        if (image->sdk_fixed && !trimmed && (module == 0u || module == image->module_id)) {
            write_be32(expected + 0x2Cu, at - imports);
            trimmed = true;
        }
    }
    return NULL;
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
        error = ww_rel_sdk_structure(image, raw, expected, next);
    }
    if (error == NULL) {
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

static const char* prepare(WwHost* host, WwRelImage* image, bool fixed) {
    if (host == NULL || image == NULL || host->rel_sdk_failed || image->certificate == NULL ||
        image->bytes != image->certificate->rel_size || image->bytes > image->capacity ||
        image != ww_host_rel_owned(host, image->owner) || image != ww_host_rel_live(host, image->module_id) ||
        image->certificate != ww_host_rel_certificate(host, image->module_id))
        return "REL SDK preparation requires its exact admitted live image";
    const bool title = ww_host_rel_profile_enabled(host) && image->module_id == 400u;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const bool actor = ww_host_actor_sdk_contract(host, image->module_id) == WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO;
    const u32 form = title ? WW_REL_SDK_TITLE_INLINE_ZERO : actor ? WW_REL_SDK_CERTIFIED_INLINE_ZERO : WW_REL_SDK_OWNER_PUBLISHED;
    if (image->sdk_input_form != WW_REL_SDK_OWNER_PUBLISHED && image->sdk_input_form != form)
        return "REL SDK preparation has an unknown or transplanted frozen input form";
    if (image->sdk_phase != WW_REL_SDK_NONE && image->sdk_input_form != form)
        return "REL SDK preparation changed its frozen policy-selected input form";
    if (actor) {
        const char* error = ww_host_rel_actor_entry(host, image, fixed);
        if (error != NULL)
            return error;
    }
#else
    if (image->sdk_input_form != WW_REL_SDK_OWNER_PUBLISHED && image->sdk_input_form != WW_REL_SDK_TITLE_INLINE_ZERO)
        return "REL SDK preparation has an unknown frozen input form";
    if (image->sdk_input_form == WW_REL_SDK_TITLE_INLINE_ZERO && !title)
        return "REL SDK inline-zero input form requires the active admitted title";
#endif
    if (title) {
        const char* error = ww_host_rel_title_entry(host, image, fixed);
        if (error != NULL)
            return input_refusal(host, image, fixed, host->cpu.gpr[4], WW_REL_SDK_TITLE_INLINE_ZERO, error);
    }
    u8* live = NULL;
    if (!ppc_guest_alias_get_storage(image->guest, image->capacity, &live))
        return "REL SDK preparation lost its owned raw storage";
    if (host->rel_sdk_pending_owner != 0u) {
        u64 candidate_hash = 0u, tail_hash = 0u;
        return host->rel_sdk_pending_owner == image->owner && image->sdk_phase == WW_REL_SDK_PENDING &&
            image->sdk_fixed == fixed && inputs_match(host, image) &&
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
            (!(title || actor) || verify_list(host, NULL, &candidate_hash, &tail_hash)) &&
#else
            (!title || verify_list(host, NULL, &candidate_hash, &tail_hash)) &&
#endif
            memcmp(live, image->certificate->rel_bytes, image->bytes) == 0 ?
            NULL : "REL SDK supports only one unchanged pending append";
    }
    if (image->sdk_phase != WW_REL_SDK_NONE || !ww_host_rel_raw_unchanged(image) ||
        memcmp(live, image->certificate->rel_bytes, image->bytes) != 0)
        return "REL SDK preparation requires the certified original raw image";
    const u8* raw = image->certificate->rel_bytes;
    const u32 owner_bss = mem_read32(&host->cpu, image->owner + 0x14u);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    WwRelBlock* actor_block = actor ? ww_host_rel_actor_block(host, image) : NULL;
    const u32 bss = actor ? actor_block->guest + actor_block->extent :
        title ? host->rel_title_block.guest + host->rel_title_block.extent : owner_bss;
#else
    const u32 bss = title ? host->rel_title_block.guest + host->rel_title_block.extent : owner_bss;
#endif
    const u32 bss_size = bluewake_archive_be32(raw + 0x20u);
    const u32 alignment = bluewake_archive_be32(raw + 0x44u);
    const u32 image_alignment = bluewake_archive_be32(raw + 0x40u);
    const u32 name_base = mem_read32(&host->cpu, WW_REL_SDK_NAMES);
    const u64 name = (u64)name_base + bluewake_archive_be32(raw + 0x14u);
    const u32 name_size = bluewake_archive_be32(raw + 0x18u);
    if ((image_alignment != 0u && image->guest % image_alignment != 0u) ||
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        (!(title || actor) && bss_size == 0u && bss != 0u) || (bss_size != 0u &&
#else
        (!title && bss_size == 0u && bss != 0u) || (bss_size != 0u &&
#endif
        ((bss & 3u) != 0u || bss < GC_RAM_BASE || (u64)bss + bss_size > GC_RAM_BASE + GC_MAIN_RAM_SIZE ||
         (alignment != 0u && bss % alignment != 0u) || ww_host_linked_alias_overlaps(bss, bss_size))) ||
        name > UINT32_MAX || (name != 0u && (name < GC_RAM_BASE || name >= GC_RAM_BASE + GC_MAIN_RAM_SIZE)) ||
        (name_size != 0u && (name < GC_RAM_BASE ||
        name + name_size > GC_RAM_BASE + GC_MAIN_RAM_SIZE || ww_host_linked_alias_overlaps((u32)name, name_size))))
        return input_refusal(host, image, fixed, bss,
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
            form,
#else
            title ? WW_REL_SDK_TITLE_INLINE_ZERO : WW_REL_SDK_OWNER_PUBLISHED,
#endif
            "REL SDK name or BSS input is not a validated bounded MEM1 range");
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
            if (ww_host_rel_profile_enabled(host) && !ww_host_rel_profile_completed(host, profile))
                return "REL SDK resident profile requires its observed guest completion";
        }
    } else if (!ww_host_rel_sdk_integrity(host)) {
        return "REL SDK existing list authority changed before append";
    }
    if (ww_host_rel_profile_enabled(host) && image->module_id == 168u &&
        fprintf(stderr, "[rel-sdk] module=168 stage=prepared fixed=%u resident=%08X certificate=%s certified=0\n",
                fixed, profile == NULL ? host->rel_sdk_head : profile->guest, image->certificate->build_identity) < 0)
        return "REL SDK preparation trace failed";
    if (title && fprintf(stderr, "[rel-sdk] module=400 stage=prepared fixed=%u input_form=%u pc=%08X caller=%08X owner=%08X module_arg=%08X name_base=%08X bss=%08X owner_bss=%08X raw_fix=%u extent=%u previous=%08X certificate=%s certified=0\n",
        fixed, WW_REL_SDK_TITLE_INLINE_ZERO, host->cpu.pc, host->cpu.lr, image->owner, host->cpu.gpr[3],
        name_base, bss, owner_bss, host->rel_title_block.raw_fix, host->rel_title_block.extent,
        host->rel_sdk_tail, image->certificate->build_identity) < 0)
        return "REL SDK title preparation trace failed";
#if WW05_ENABLE_IKADA_CONTRACT
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!ikada_trace(host, image, fixed, bss, name_base, "prepared", "not-verified", form))
#else
    if (!ikada_trace(host, image, fixed, bss, name_base, "prepared", "not-verified"))
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        return "REL admitted actor SDK preparation trace failed";
#else
        return "REL ikada SDK preparation trace failed";
#endif
#endif
    if (profile != NULL) {
        profile->sdk_phase = WW_REL_SDK_RESIDENT;
        host->rel_sdk_head = profile->guest;
        host->rel_sdk_tail = profile->guest;
    }
    host->rel_sdk_seeded = true;
    image->sdk_name_base = name_base;
    image->sdk_bss = bss;
    image->sdk_fixed = fixed;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    image->sdk_input_form = form;
#else
    image->sdk_input_form = title ? WW_REL_SDK_TITLE_INLINE_ZERO : WW_REL_SDK_OWNER_PUBLISHED;
#endif
    image->sdk_previous = host->rel_sdk_tail;
    image->sdk_next = 0u;
    image->sdk_phase = WW_REL_SDK_PENDING;
    host->rel_sdk_pending_owner = image->owner;
    if (title)
        ww_host_rel_title_entered(host);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (actor)
        ww_host_rel_actor_entered(actor_block);
#endif
    return NULL;
}

const char* ww_host_rel_sdk_prepare(WwHost* host, WwRelImage* image, bool fixed) {
    const char* error = prepare(host, image, fixed);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (error != NULL && host != NULL)
        ww_host_rel_actor_failed(host, image);
#endif
#if WW05_ENABLE_IKADA_CONTRACT
    if (error != NULL)
        error = ikada_refusal(host, image, fixed, error);
#endif
    if (error != NULL && host != NULL && image != NULL &&
        ww_host_rel_profile_enabled(host) && image->module_id == 400u)
        ww_host_rel_title_failed(host);
    return error;
}

static const char* finish(WwHost* host, WwRelImage* image) {
    if (host == NULL || image == NULL || host->rel_sdk_failed ||
        ww_host_rel_owned(host, image->owner) != image)
        return "REL SDK certification requires an intact pending authority";
    if (image->sdk_phase == WW_REL_SDK_CERTIFIED)
        return ww_host_rel_sdk_integrity(host) ? NULL : "REL SDK certified list integrity changed";
    if (host->rel_sdk_pending_owner != image->owner || image->sdk_phase != WW_REL_SDK_PENDING)
        return "REL SDK certification requires its prepared append";
    const bool title = image->sdk_input_form == WW_REL_SDK_TITLE_INLINE_ZERO;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const bool actor = ww_host_actor_sdk_contract(host, image->module_id) == WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO;
    WwRelBlock* actor_block = actor ? ww_host_rel_actor_block(host, image) : NULL;
    const u32 form = ww_host_rel_profile_enabled(host) && image->module_id == 400u ? WW_REL_SDK_TITLE_INLINE_ZERO :
        actor ? WW_REL_SDK_CERTIFIED_INLINE_ZERO : WW_REL_SDK_OWNER_PUBLISHED;
    if (image->sdk_input_form != form)
        return "REL SDK certification requires its frozen policy-selected input form";
    if (actor) {
        const char* error = ww_host_rel_actor_return(host, image);
        if (error != NULL)
            return error;
    }
#endif
    if (ww_host_rel_profile_enabled(host) && image->module_id == 400u && !title)
        return "REL SDK active title requires its frozen inline-zero input contract";
    if (title) {
        const char* error = ww_host_rel_title_return(host, image);
        if (error != NULL)
            return error;
    }
    u64 candidate_hash = 0u, tail_hash = 0u;
    if (!verify_list(host, image, &candidate_hash, &tail_hash)) {
        host->rel_sdk_failed = true;
        image->sdk_phase = WW_REL_SDK_FAILED;
        return "REL SDK complete image, neighbors or publications disagree with expected append";
    }
    if (ww_host_rel_profile_enabled(host) && image->module_id == 168u &&
        fprintf(stderr, "[rel-sdk] module=168 stage=verified fixed=%u resident=%08X whole_image=matched certificate=%s certified=1\n",
                image->sdk_fixed, host->rel_sdk_head, image->certificate->build_identity) < 0)
        return "REL SDK verification trace failed";
    if (title && fprintf(stderr, "[rel-sdk] module=400 stage=verified fixed=1 input_form=%u pc=%08X caller=%08X result=%u whole_image=matched certificate=%s certified=1\n",
        image->sdk_input_form, host->cpu.pc, host->cpu.lr, host->cpu.gpr[3], image->certificate->build_identity) < 0)
        return "REL SDK title verification trace failed";
#if WW05_ENABLE_IKADA_CONTRACT
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!ikada_trace(host, image, image->sdk_fixed, image->sdk_bss, image->sdk_name_base, "verified", "matched", image->sdk_input_form))
#else
    if (!ikada_trace(host, image, image->sdk_fixed, image->sdk_bss, image->sdk_name_base, "verified", "matched"))
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        return "REL admitted actor SDK verification trace failed";
#else
        return "REL ikada SDK verification trace failed";
#endif
#endif
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
    if (title)
        ww_host_rel_title_returned(host);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (actor)
        ww_host_rel_actor_returned(actor_block);
#endif
    return NULL;
}

const char* ww_host_rel_sdk_finish(WwHost* host, WwRelImage* image) {
    const char* error = finish(host, image);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (error != NULL && host != NULL)
        ww_host_rel_actor_failed(host, image);
#endif
    if (error != NULL && host != NULL && image != NULL &&
        ww_host_rel_profile_enabled(host) && image->module_id == 400u)
        ww_host_rel_title_failed(host);
    return error;
}

const char* ww_host_rel_sdk_observe(WwHost* host, u32 address) {
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    bool actor_handled = false;
    const char* actor_error = ww_host_rel_actor_observe(host, address, &actor_handled);
    if (actor_handled || actor_error != NULL)
        return actor_error;
#endif
    WwRelBlock* title = &host->rel_title_block;
    const bool active = ww_host_rel_profile_enabled(host);
    if (active && address != title->window)
        title->window = 0u;
    const char* profile_error = ww_host_rel_profile_observe(host, address);
    if (profile_error != NULL)
        return profile_error;
    if (active && address == 0x80240D4Cu && title->phase != WW_REL_PROFILE_NONE &&
        (title->phase == WW_REL_PROFILE_SDK || host->cpu.gpr[31] == title->owner)) {
        const char* error = ww_host_rel_title_return(host, title->image);
        return error != NULL ? error : ww_host_rel_sdk_finish(host, title->image);
    }
    if (address != 0x80305448u && address != 0x80305424u)
        return NULL;
    WwRelImage* image = image_at(host, host->cpu.gpr[3]);
    if (active && title->phase != WW_REL_PROFILE_NONE &&
        (title->phase == WW_REL_PROFILE_SIZE || title->phase == WW_REL_PROFILE_SDK ||
         host->cpu.gpr[31] == title->owner || image == title->image))
        return ww_host_rel_sdk_prepare(host, title->image, address == 0x80305448u);
    if (image == NULL || image->certificate == NULL)
        return NULL;
    if (!ww_host_module_contains_address(host->module, address) ||
        (host->cpu.pc & ~0x40000000u) != address)
        return "REL SDK observation requires its owned translated entry PC";
    if (host->cpu.gpr[4] != mem_read32(&host->cpu, image->owner + 0x14u) &&
        !(active && image->module_id == 400u))
#if WW05_ENABLE_IKADA_CONTRACT
        return ikada_refusal(host, image, address == 0x80305448u,
                             "REL SDK observed BSS argument disagrees with its owner");
#else
        return "REL SDK observed BSS argument disagrees with its owner";
#endif
    return ww_host_rel_sdk_prepare(host, image, address == 0x80305448u);
}
