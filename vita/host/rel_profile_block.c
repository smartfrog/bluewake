#include "boot_host.h"
#include "rel_archive.h"
#include "rel_profile_list.h"
#include <stdlib.h>

#define PROFILE_PROLOG 0xC1F800D4u
#define PROFILE_CTOR_RETURN 0xC1F800ECu
#define PROFILE_RETURN 0x80240EE8u
#define SDK_FIXED 0x80305448u
#define SDK_RETURN 0x80240D4Cu
#define BLOCK_SIZE 0x802B07ECu
#define BLOCK_SHRINK 0x802B0704u

bool ww_host_rel_profile_enabled(const WwHost* host) {
    return host->ship_contract != NULL && host->rel_title_admission;
}

static const char* block_refuse(WwRelBlock* block, const char* error) {
    block->phase = WW_REL_PROFILE_FAILED;
    block->window = 0u;
    return error;
}

static const char* refuse(WwHost* host, const char* error) {
    return block_refuse(&host->rel_profile_block.allocation, error);
}

void ww_host_rel_profile_revoke(WwHost* host) {
    WwRelBlock* block = &host->rel_profile_block.allocation;
    if (block->phase != WW_REL_PROFILE_NONE && block->phase != WW_REL_PROFILE_COMPLETE)
        refuse(host, "REL resident profile completion revoked");
    block = &host->rel_title_block;
    if (block->phase != WW_REL_PROFILE_NONE && (block->image == NULL || !block->image->initialization_completed))
        block_refuse(block, "REL title allocation sequence revoked");
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    for (u32 at = 0u; at < host->rel_actor_block_count; ++at) {
        block = &host->rel_actor_blocks[at];
        if (block->phase != WW_REL_PROFILE_NONE && (block->image == NULL || !block->image->initialization_completed))
            block_refuse(block, "REL actor allocation sequence revoked");
    }
#endif
}

#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
const char* ww_rel_inline_layout(const BlueWakeRelInitContractV1* certificate) {
    if (certificate == NULL || certificate->rel_bytes == NULL || certificate->rel_size < 0x4Cu)
        return "REL inline allocation requires immutable certified header bytes";
    const u8* raw = certificate->rel_bytes;
    const u32 bytes = certificate->rel_size;
    const u32 fix = bluewake_archive_be32(raw + 0x48u);
    const u64 extent = ((u64)fix + 31u) & ~31ull;
    const u64 capacity = ((u64)bytes + 31u) & ~31ull;
    const u32 table = bluewake_archive_be32(raw + 0x10u), count = bluewake_archive_be32(raw + 0xCu);
    if (bluewake_archive_be32(raw + 0x1Cu) != 3u || bluewake_archive_be32(raw + 0x20u) != 0u ||
        fix < 0x4Cu || extent > UINT32_MAX || extent >= bytes || capacity > UINT32_MAX || extent + 16u >= capacity ||
        count == 0u || count > 256u || table < 0x4Cu || (u64)table + (u64)count * 8u > extent ||
        (u64)bluewake_archive_be32(raw + 0x28u) + bluewake_archive_be32(raw + 0x2Cu) > extent)
        return "REL inline allocation requires version-3 zero-BSS retained metadata and bounded slack";
    for (u32 at = 0u; at < count; ++at) {
        const u32 offset = bluewake_archive_be32(raw + table + at * 8u) & ~1u;
        const u32 size = bluewake_archive_be32(raw + table + at * 8u + 4u);
        if (size != 0u && (offset == 0u || (u64)offset + size > extent))
            return "REL inline allocation requires retained file-backed sections and no nonempty BSS section";
    }
    return NULL;
}

static WwRelBlock* actor_record(WwHost* host, const WwRelImage* image) {
    if (image == NULL)
        return NULL;
    WwRelBlock* found = NULL;
    for (u32 at = 0u; at < host->rel_actor_block_count; ++at)
        if (host->rel_actor_blocks[at].image == image || (host->rel_actor_blocks[at].image == NULL &&
            image->certificate != NULL && host->rel_actor_blocks[at].certificate == image->certificate)) {
            if (found != NULL)
                return NULL;
            found = &host->rel_actor_blocks[at];
        }
    return found;
}

WwRelBlock* ww_host_rel_actor_block(WwHost* host, const WwRelImage* image) {
    if (image == NULL || ww_host_actor_sdk_contract(host, image->module_id) != WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO ||
        image->certificate == NULL || image->certificate != ww_host_actor_batch_certificate(host, image->module_id))
        return NULL;
    WwRelBlock* block = actor_record(host, image);
    return block != NULL && block->certificate == image->certificate ? block : NULL;
}
#endif

static const u8* block_raw(const WwHost* host, const WwRelBlock* block) {
    return block == &host->rel_profile_block.allocation ? host->rel_profile_block.raw : block->identity;
}

static bool identity(WwHost* host, const WwRelBlock* block) {
    const WwRelImage* image = block->image;
    const u8* raw = block_raw(host, block);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const bool profile = block == &host->rel_profile_block.allocation;
    const bool title = block == &host->rel_title_block;
    if (!profile && !title && (image == NULL || ww_host_rel_actor_block(host, image) != block))
        return false;
    const u32 module = profile ? 1u : title ? 400u : block->certificate->module_id;
    if (block->owner < GC_RAM_BASE || (u64)block->owner + 0x18u > GC_RAM_BASE + GC_MAIN_RAM_SIZE ||
        ww_host_linked_alias_overlaps(block->owner, 0x18u))
        return false;
#else
    const u32 module = block == &host->rel_profile_block.allocation ? 1u : 400u;
#endif
    u8* live = NULL;
    if (!ww_host_rel_profile_enabled(host) || host->error != NULL || image == NULL || raw == NULL ||
        block->phase == WW_REL_PROFILE_FAILED || image != ww_host_rel_owned(host, block->owner) ||
        image != ww_host_rel_live(host, module) || image->module_id != module || !image->linked || image->unlinked ||
        (module == 1u ? image->certificate != NULL :
         (block->certificate == NULL || image->certificate != block->certificate ||
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
          block->certificate != ww_host_rel_certificate(host, module) ||
#else
          block->certificate != ww_host_rel_certificate(host, 400u) ||
#endif
          block->certificate->rel_bytes != block->identity || block->certificate->rel_size != block->bytes)) ||
        image->owner != block->owner || image->guest != block->guest || image->bytes != block->bytes ||
        image->capacity != block->capacity || image->sections != block->sections ||
        mem_read32(&host->cpu, block->owner + 0x10u) != block->guest ||
        mem_read32(&host->cpu, block->owner + 0x14u) != 0u ||
        (module == 1u && mem_read32(&host->cpu, BLUEWAKE_PROFILE_LIST_GLOBAL) != host->rel_profile_block.publication) ||
        !ppc_guest_alias_get_storage(block->guest, block->capacity, &live) || live != block->storage)
        return false;
    const u32 count = bluewake_archive_be32(raw + 0xCu);
    const u32 table = bluewake_archive_be32(raw + 0x10u);
    if (image->section_count != count || image->section_table != table || count > 256u ||
        (u64)table + (u64)count * 8u > block->bytes || image->sections == NULL)
        return false;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (module != 1u && (bluewake_archive_be32(raw + 0x1Cu) != 3u ||
#else
    if (module == 400u && (bluewake_archive_be32(raw + 0x1Cu) != 3u ||
#endif
        bluewake_archive_be32(raw + 0x20u) != 0u || bluewake_archive_be32(raw + 0x48u) != block->raw_fix ||
        block->extent != (((u64)block->raw_fix + 31u) & ~31ull) ||
        block->raw_fix < 0x4Cu || block->extent >= block->bytes || block->bytes > block->capacity ||
        (u64)block->extent + 16u >= block->capacity || block->guest < WW_REL_SCRATCH_BASE ||
        (u64)block->guest + block->capacity > WW_REL_SCRATCH_LIMIT))
        return false;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!profile && !title && (ww_rel_inline_layout(block->certificate) != NULL ||
        block->guest % (bluewake_archive_be32(raw + 0x40u) == 0u ? 1u : bluewake_archive_be32(raw + 0x40u)) != 0u ||
        (u64)block->guest + block->extent > UINT32_MAX || ((block->guest + block->extent) & 3u) != 0u ||
        (block->guest + block->extent) % (bluewake_archive_be32(raw + 0x44u) == 0u ? 1u : bluewake_archive_be32(raw + 0x44u)) != 0u))
        return false;
#endif
    for (u32 at = 0u; at < host->rel_image_count; ++at) {
        const WwRelImage* other = &host->rel_images[at];
        if (other != image && other->guest != 0u && (other->module_id == module || other->owner == block->owner ||
            ((u64)other->guest < (u64)block->guest + block->capacity &&
             (u64)block->guest < (u64)other->guest + other->capacity)))
            return false;
    }
    const StaticRecompRelModule* rel = ww_host_rel_descriptor(host, module);
    if (rel == NULL)
        return false;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!profile && !title && (rel != block->descriptor || rel->module_id != module ||
        rel->file_size != block->bytes || rel->section_info_offset != table || rel->section_count != count))
        return false;
#endif
    for (u32 at = 0u; at < count; ++at) {
        const WwRelSection* section = &image->sections[at];
        const StaticRecompRelSection* compiled = ww_rel_section(rel, at);
        if (section->flags != bluewake_archive_be32(raw + table + at * 8u) ||
            section->size != bluewake_archive_be32(raw + table + at * 8u + 4u) ||
            section->linked_start != (compiled == NULL ? 0u : compiled->linked_start))
            return false;
    }
    return true;
}

static const char* expose(WwHost* host, WwRelImage* image, WwRelBlock* block, const u8* raw) {
    u8* storage = NULL;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const bool title = block == &host->rel_title_block ||
        (image != NULL && ww_host_rel_actor_block(host, image) == block);
    if (block != &host->rel_profile_block.allocation && !title)
        return block_refuse(block, "REL bounded allocation requires an explicit profile, title or admitted actor role");
#else
    const bool title = block == &host->rel_title_block;
#endif
    if (host->error != NULL || block->phase != WW_REL_PROFILE_NONE || image == NULL || raw == NULL ||
        image->bytes < 0x4Cu || image->bytes > 0x100000u || image->bytes > image->capacity || !image->linked ||
        !ww_host_rel_raw_unchanged(image) || !ww_host_rel_sections_match(image) ||
        !ppc_guest_alias_get_storage(image->guest, image->capacity, &storage) ||
        memcmp(storage, raw, image->bytes) != 0 || bluewake_archive_be32(raw + 0x1Cu) != 3u ||
        bluewake_archive_be32(raw + 0x20u) != 0u)
        return block_refuse(block, "REL bounded allocation requires its original version-3 zero-BSS image");
    const u32 fix = bluewake_archive_be32(raw + 0x48u);
    const u64 extent = ((u64)fix + 31u) & ~31ull;
    const u64 bss = (u64)image->guest + extent;
    const u32 align = bluewake_archive_be32(raw + 0x40u), bss_align = bluewake_archive_be32(raw + 0x44u);
    if ((title ? fix < 0x4Cu : extent < 0x4Cu) || extent > UINT32_MAX || bss > UINT32_MAX || extent > image->bytes ||
        (title && extent == image->bytes) || extent + 16u >= image->capacity ||
        image->guest < WW_REL_SCRATCH_BASE || (u64)image->guest + image->capacity >
            (title ? WW_REL_SCRATCH_LIMIT : WW_REL_MODULE_REGION_LIMIT) ||
        (align != 0u && image->guest % align != 0u) || (bss & 3u) != 0u ||
        (bss_align != 0u && bss % bss_align != 0u))
        return block_refuse(block, "REL bounded allocation has an invalid retained extent or alignment");
    if (title) {
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        const char* error = ww_rel_inline_layout(image->certificate);
        if (error != NULL)
            return block_refuse(block, error);
#else
        const u32 table = image->section_table, count = image->section_count;
        const u64 imports_end = (u64)bluewake_archive_be32(raw + 0x28u) + bluewake_archive_be32(raw + 0x2Cu);
        if ((u64)table + (u64)count * 8u > extent || imports_end > extent)
            return block_refuse(block, "REL title retained metadata exceeds its logical extent");
        for (u32 at = 0u; at < count; ++at) {
            const WwRelSection* section = &image->sections[at];
            if (section->size != 0u && ((section->flags & ~1u) == 0u ||
                (u64)(section->flags & ~1u) + section->size > extent))
                return block_refuse(block, "REL title retained sections require bounded file-backed zero-BSS storage");
        }
#endif
    }
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    *block = (WwRelBlock){.descriptor=ww_host_rel_descriptor(host, image->module_id),
        .phase=WW_REL_PROFILE_EXPOSED,.image=image,.sections=image->sections,
#else
    *block = (WwRelBlock){.phase=WW_REL_PROFILE_EXPOSED,.image=image,.sections=image->sections,
#endif
        .certificate=image->certificate,.identity=title ? raw : NULL,.storage=storage,
        .owner=image->owner,.guest=image->guest,.bytes=image->bytes,.capacity=image->capacity,
        .raw_fix=fix,.extent=(u32)extent,.logical_size=image->capacity};
    if (!identity(host, block))
        return block_refuse(block, "REL bounded allocation ownership is inconsistent");
    return NULL;
}

const char* ww_host_rel_profile_expose(WwHost* host, WwRelImage* image) {
    if (!ww_host_rel_profile_enabled(host))
        return NULL;
    if (host->error != NULL)
        return refuse(host, host->error);
    WwRelBlock* block = &host->rel_profile_block.allocation;
    if (block->phase != WW_REL_PROFILE_NONE)
        return refuse(host, "REL resident profile allocation cannot be exposed twice");
    u8* raw = NULL;
    const BlueWakeRelData* profile = bluewake_rel_profile_list(host->rel_data, host->rel_data_count, 1u);
    if (image == NULL || image->module_id != 1u || image->certificate != NULL || !image->linked ||
        image->bytes < 0x4Cu || image->bytes > 0x100000u || image->bytes > image->capacity ||
        !ww_host_rel_raw_unchanged(image) || !ww_host_rel_sections_match(image) || profile == NULL ||
        !ppc_guest_alias_get_storage(image->guest, image->capacity, &raw) ||
        bluewake_archive_be32(raw + 0x1Cu) != 3u || bluewake_archive_be32(raw + 0x20u) != 0u)
        return refuse(host, "REL resident profile allocation requires its original version-3 zero-BSS image");
    if (raw[0x30u] >= image->section_count || raw[0x30u] == 0u ||
        image->sections[raw[0x30u]].linked_start + bluewake_archive_be32(raw + 0x34u) != PROFILE_PROLOG)
        return refuse(host, "REL resident profile allocation has an invalid bounded resident extent or prolog");
    u8* frozen = malloc(image->bytes);
    if (frozen == NULL)
        return refuse(host, "REL resident profile verification allocation failed");
    memcpy(frozen, raw, image->bytes);
    host->rel_profile_block.raw = frozen;
    host->rel_profile_block.publication = profile->linked_start;
    const char* error = expose(host, image, block, frozen);
    if (error != NULL)
        return error;
    if (fprintf(stderr, "[rel-profile-block] stage=exposed guest=%08X owner=%08X bytes=%u capacity=%u logical=%u extent=%u certified=0\n",
                block->guest, block->owner, block->bytes, block->capacity, block->logical_size, block->extent) < 0)
        return refuse(host, "REL resident profile allocation trace failed");
    return NULL;
}

static bool original(WwHost* host, const WwRelBlock* block) {
    u8* live = NULL;
    return identity(host, block) && ww_host_rel_sections_match(block->image) &&
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        (ww_host_rel_raw_unchanged(block->image) || (block != &host->rel_profile_block.allocation &&
#else
        (ww_host_rel_raw_unchanged(block->image) || (block == &host->rel_title_block &&
#endif
         block->image->sdk_phase == WW_REL_SDK_PENDING && block->image->raw_hash == ww_host_rel_raw_hash(block_raw(host, block), block->bytes))) &&
        ppc_guest_alias_get_storage(block->guest, block->capacity, &live) &&
        memcmp(live, block_raw(host, block), block->bytes) == 0;
}

static bool intact(WwHost* host, const WwRelBlock* block) {
    return identity(host, block) && ww_host_rel_sections_match_post_link(block->image) &&
        ww_host_rel_raw_unchanged(block->image) &&
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        (block != &host->rel_profile_block.allocation ? ww_host_rel_sdk_integrity(host) :
#else
        (block == &host->rel_title_block ? ww_host_rel_sdk_integrity(host) :
#endif
         mem_read32(&host->cpu, 0x800030D0u) == host->rel_profile_block.name_base);
}

/* The guest's unload path re-queries the same block size after its successful
 * OSUnlink; only the block whose own reconciliation unlinked the image answers
 * (the post-link section comparison requires a linked image). */
static bool unlinked_block(WwHost* host, const WwRelBlock* block) {
    const WwRelImage* image = block->image;
    return image != NULL && image->unlinked && !image->linked &&
        image == ww_host_rel_owned(host, block->owner) && ww_host_rel_raw_unchanged(image);
}

const char* ww_host_rel_profile_observe(WwHost* host, u32 address) {
    WwRelBlock* block = &host->rel_profile_block.allocation;
    if (!ww_host_rel_profile_enabled(host) || block->phase == WW_REL_PROFILE_NONE)
        return NULL;
    address &= ~0x40000000u;
    if (address != block->window)
        block->window = 0u;
    CPUState* cpu = &host->cpu;
    if ((address == SDK_FIXED || address == 0x80305424u) &&
        (block->phase == WW_REL_PROFILE_SIZE || block->phase == WW_REL_PROFILE_SDK ||
         (cpu->gpr[3] >= block->guest && (u64)cpu->gpr[3] < (u64)block->guest + block->capacity))) {
        if (address != SDK_FIXED || cpu->pc != SDK_FIXED || cpu->exception != 0u || cpu->lr != SDK_RETURN ||
            cpu->gpr[3] != block->guest || cpu->gpr[4] != block->guest + block->extent ||
            cpu->gpr[31] != block->owner || !ww_host_module_contains_address(host->module, address) ||
            !ww_host_module_contains_address(host->module, SDK_RETURN) || !original(host, block) ||
            !((block->phase == WW_REL_PROFILE_SIZE) ||
              (block->phase == WW_REL_PROFILE_SDK && block->window == SDK_FIXED)) ||
            host->rel_sdk_seeded || host->rel_sdk_pending_owner != 0u || host->rel_sdk_failed ||
            mem_read32(cpu, 0x800030C8u) != 0u || mem_read32(cpu, 0x800030CCu) != 0u)
            return refuse(host, "REL resident profile requires its exact first fixed SDK entry");
        const u32 names = mem_read32(cpu, 0x800030D0u);
        const u64 name = (u64)names + bluewake_archive_be32(host->rel_profile_block.raw + 0x14u);
        const u32 name_size = bluewake_archive_be32(host->rel_profile_block.raw + 0x18u);
        if (name > UINT32_MAX || (name != 0u && (name < GC_RAM_BASE || name >= GC_RAM_BASE + GC_MAIN_RAM_SIZE)) ||
            (name_size != 0u && (name < GC_RAM_BASE || name + name_size > GC_RAM_BASE + GC_MAIN_RAM_SIZE ||
             ww_host_linked_alias_overlaps((u32)name, name_size))) ||
            (block->phase == WW_REL_PROFILE_SDK && host->rel_profile_block.name_base != names))
            return refuse(host, "REL resident profile SDK string-table input changed or is unbounded");
        if (block->phase != WW_REL_PROFILE_SDK &&
            fprintf(stderr, "[rel-profile-sdk] stage=entered pc=%08X module=%08X bss=%08X caller=%08X certified=0\n",
                    address, cpu->gpr[3], cpu->gpr[4], cpu->lr) < 0)
            return refuse(host, "REL resident profile SDK entry trace failed");
        host->rel_profile_block.name_base = names;
        block->phase = WW_REL_PROFILE_SDK;
        block->window = SDK_FIXED;
    } else if (address == SDK_RETURN && (block->phase == WW_REL_PROFILE_SDK ||
               (block->phase == WW_REL_PROFILE_RETURNED && block->window == SDK_RETURN))) {
        if (cpu->pc != address || cpu->exception != 0u || cpu->gpr[3] != 1u || cpu->lr != SDK_RETURN ||
            cpu->gpr[31] != block->owner || !identity(host, block) ||
            mem_read32(cpu, 0x800030D0u) != host->rel_profile_block.name_base ||
            mem_read32(cpu, 0x800030C8u) != block->guest || mem_read32(cpu, 0x800030CCu) != block->guest)
            return refuse(host, "REL resident profile SDK return requires its successful guest singleton");
        if (block->phase == WW_REL_PROFILE_RETURNED)
            return intact(host, block) ? NULL : refuse(host, "REL resident profile SDK duplicate return changed");
        u8* expected = malloc(block->bytes);
        if (expected == NULL)
            return refuse(host, "REL resident profile SDK reconstruction allocation failed");
        memcpy(expected, host->rel_profile_block.raw, block->bytes);
        WwRelImage sdk = *block->image;
        sdk.sdk_fixed = true;
        sdk.sdk_previous = 0u;
        sdk.sdk_bss = block->guest + block->extent;
        sdk.sdk_name_base = host->rel_profile_block.name_base;
        const char* error = ww_rel_replay_resident(host, ww_host_rel_descriptor(host, 1u),
            host->rel_profile_block.raw, block->bytes, expected, &sdk);
        if (error == NULL)
            error = ww_rel_sdk_structure(&sdk, host->rel_profile_block.raw, expected, 0u);
        u8* live = NULL;
        if (error == NULL && (!ppc_guest_alias_get_storage(block->guest, block->capacity, &live) ||
            memcmp(expected, live, block->bytes) != 0))
            error = "REL resident profile differs from its independent whole-image reconstruction";
        const u64 hash = ww_host_rel_raw_hash(expected, block->bytes);
        free(expected);
        if (error != NULL)
            return refuse(host, error);
        if (fprintf(stderr, "[rel-profile-sdk] stage=returned result=%u head=%08X tail=%08X whole_image=matched old_hash=%016llX new_hash=%016llX certified=0\n",
                    cpu->gpr[3], block->guest, block->guest, (unsigned long long)block->image->raw_hash,
                    (unsigned long long)hash) < 0)
            return refuse(host, "REL resident profile SDK return trace failed");
        block->image->raw_hash = hash;
        block->phase = WW_REL_PROFILE_RETURNED;
        block->window = SDK_RETURN;
    } else if (address == SDK_RETURN && cpu->gpr[31] == block->owner) {
        return refuse(host, "REL resident profile SDK return is outside its active observation window");
    }
    return NULL;
}

static WwRelBlock* select_block(WwHost* host, u32 pointer, u32 owner) {
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    WwRelBlock* found = NULL;
    const u32 canonical = pointer & ~0x40000000u;
    for (u32 at = 0u; at < 2u + host->rel_actor_block_count; ++at) {
        WwRelBlock* block = at == 0u ? &host->rel_profile_block.allocation :
            at == 1u ? &host->rel_title_block : &host->rel_actor_blocks[at - 2u];
        WwRelImage* image = block->image;
        if (block == &host->rel_profile_block.allocation && block->phase == WW_REL_PROFILE_NONE)
            continue;
        if (image == NULL && block == &host->rel_title_block)
            image = ww_host_rel_live(host, 400u);
        if (image == NULL && block->certificate != NULL)
            image = ww_host_rel_live(host, block->certificate->module_id);
        if (image == NULL)
            continue;
        const bool exposed = block->image != NULL;
        const u32 base = exposed ? block->guest : image->guest;
        const u32 capacity = exposed ? block->capacity : image->capacity;
        if (owner != (exposed ? block->owner : image->owner) && !(canonical >= base && (u64)canonical < (u64)base + capacity))
            continue;
        if (found != NULL && found != block) {
            block_refuse(found, "REL bounded allocation selection is ambiguous");
            block_refuse(block, "REL bounded allocation selection is ambiguous");
        }
        found = block;
    }
    return found;
#else
    WwRelBlock* profile = &host->rel_profile_block.allocation;
    WwRelBlock* title = &host->rel_title_block;
    const u32 canonical = pointer & ~0x40000000u;
    const bool profile_match = profile->phase != WW_REL_PROFILE_NONE &&
        canonical >= profile->guest && (u64)canonical < (u64)profile->guest + profile->capacity;
    const bool title_match = title->phase != WW_REL_PROFILE_NONE && (owner == title->owner ||
        (canonical >= title->guest && (u64)canonical < (u64)title->guest + title->capacity));
    if (profile_match && title_match) {
        block_refuse(profile, "REL bounded allocation selection is ambiguous");
        block_refuse(title, "REL bounded allocation selection is ambiguous");
    }
    return title_match ? title : profile_match ? profile : NULL;
#endif
}

WwRelProfileService ww_host_rel_profile_service(WwHost* host, u32 address) {
    CPUState* cpu = &host->cpu;
    if (!ww_host_rel_profile_enabled(host) ||
        (address != BLOCK_SIZE && address != BLOCK_SHRINK) || cpu->exception != 0u)
        return WW_REL_PROFILE_MISSED;
    WwRelBlock* block = select_block(host, cpu->gpr[3], cpu->gpr[31]);
    if (block == NULL)
        return WW_REL_PROFILE_MISSED;
    const bool title = block == &host->rel_title_block;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    const bool actor = block != &host->rel_profile_block.allocation && !title;
#endif
    const bool initial = address == BLOCK_SIZE && cpu->lr == 0x80240CC4u;
    const bool accounting = address == BLOCK_SIZE && cpu->lr == 0x80241058u;
    const bool shrink = address == BLOCK_SHRINK && cpu->lr == 0x80240D80u;
    const bool unlink_accounting = accounting && cpu->pc == address && cpu->gpr[3] == block->guest &&
        cpu->gpr[4] == 0u && cpu->gpr[31] == 1u && block->phase == WW_REL_PROFILE_SHRUNK &&
        block->accounting_observed && !block->unlink_accounted &&
        ww_host_module_contains_address(host->module, address) &&
        ww_host_module_contains_address(host->module, cpu->lr) && unlinked_block(host, block);
    if (unlink_accounting) {
        block->unlink_accounted = true;
        cpu->gpr[3] = block->logical_size;
        cpu->pc = cpu->lr;
        return WW_REL_PROFILE_HANDLED;
    }
    const bool phase = initial ? (block->phase == WW_REL_PROFILE_EXPOSED ||
        (block->phase == WW_REL_PROFILE_SIZE && block->window == address)) :
        accounting ? (block->phase == WW_REL_PROFILE_SHRUNK &&
        (!block->accounting_observed || block->window == address)) :
        shrink && (block->phase == WW_REL_PROFILE_RETURNED ||
        (block->phase == WW_REL_PROFILE_SHRUNK && block->window == address && !block->accounting_observed));
    const bool valid = phase && cpu->pc == address && cpu->gpr[3] == block->guest &&
        cpu->gpr[31] == block->owner && ww_host_module_contains_address(host->module, address) &&
        ww_host_module_contains_address(host->module, cpu->lr) &&
        (initial ? original(host, block) : intact(host, block)) &&
        (shrink ? cpu->gpr[4] == block->extent && cpu->gpr[5] == 0u : cpu->gpr[4] == 0u);
    const char* error = valid ? NULL : "REL resident profile block operation has inconsistent ownership, arguments or sequence";
    const char* format = title ?
        "[rel-title-block] stage=%s guest=%08X owner=%08X logical=%u capacity=%u entry=%08X caller=%08X certified=%u\n" :
        "[rel-profile-block] stage=%s guest=%08X owner=%08X logical=%u capacity=%u entry=%08X caller=%08X certified=%u\n";
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (error == NULL && block->window != address && actor &&
        (fprintf(stderr, "[rel-actor-block] stage=%s module=%u guest=%08X owner=%08X logical=%u capacity=%u entry=%08X caller=%08X certified=%u\n",
         initial ? "size" : shrink ? "shrunk" : "accounting", block->image->module_id,
         block->guest, block->owner, shrink ? block->extent : block->logical_size, block->capacity,
         address, cpu->lr, block->image->sdk_phase == WW_REL_SDK_CERTIFIED) < 0 || fflush(stderr) != 0))
        error = "REL actor block operation trace write/flush failed";
    if (error == NULL && block->window != address && !actor && fprintf(stderr, format,
#else
    if (error == NULL && block->window != address && fprintf(stderr, format,
#endif
        initial ? "size" : shrink ? "shrunk" : "accounting",
        block->guest, block->owner, shrink ? block->extent : block->logical_size, block->capacity,
        address, cpu->lr, title && block->image->sdk_phase == WW_REL_SDK_CERTIFIED) < 0)
        error = "REL resident profile block operation trace failed";
    if (error != NULL) {
        ww_host_fail(host, block_refuse(block, error), address, cpu->pc);
        return WW_REL_PROFILE_ERROR;
    }
    if (initial) block->phase = WW_REL_PROFILE_SIZE;
    if (shrink) { block->logical_size = block->extent; block->phase = WW_REL_PROFILE_SHRUNK; }
    if (accounting) block->accounting_observed = true;
    block->window = address;
    cpu->gpr[3] = block->logical_size;
    cpu->pc = cpu->lr;
    return WW_REL_PROFILE_HANDLED;
}

static bool caller_matches(const WwHost* host) {
    const WwRelProfileBlock* profile = &host->rel_profile_block;
    const WwRelBlock* block = &profile->allocation;
    const CPUState* cpu = &host->cpu;
    return cpu->gpr[1] == profile->r1 && cpu->gpr[2] == profile->r2 && cpu->gpr[13] == profile->r13 &&
        cpu->gpr[31] == profile->r31 && profile->r31 == block->owner && cpu->lr == PROFILE_RETURN;
}

bool ww_host_rel_profile_return_qualified(WwHost* host, u32 address) {
    const WwRelBlock* block = &host->rel_profile_block.allocation;
    return block->phase == WW_REL_PROFILE_COMPLETE && block->window == PROFILE_RETURN &&
        address == PROFILE_RETURN && host->cpu.pc == address && host->cpu.exception == 0u && caller_matches(host) && intact(host, block) &&
        host->rel_pending_init.phase == WW_REL_INIT_NONE && host->rel_constructor_owner == 0u;
}

const char* ww_host_rel_profile_lifecycle(WwHost* host, u32 address) {
    WwRelBlock* block = &host->rel_profile_block.allocation;
    if (!ww_host_rel_profile_enabled(host) || block->phase == WW_REL_PROFILE_NONE)
        return NULL;
    if ((address & ~0x40000000u) != block->window)
        block->window = 0u;
    if (address == PROFILE_PROLOG) {
        if (!intact(host, block) || host->cpu.exception != 0u || host->rel_pending_init.phase != WW_REL_INIT_NONE ||
            host->cpu.lr != PROFILE_RETURN || host->cpu.gpr[3] != block->guest ||
            host->cpu.gpr[31] != block->owner || ww_host_rel_address(host, host->cpu.pc) != PROFILE_PROLOG ||
            host->rel_constructor_owner != 0u || !block->accounting_observed ||
            !(block->phase == WW_REL_PROFILE_SHRUNK ||
              (block->phase == WW_REL_PROFILE_ACTIVE && block->window == (PROFILE_PROLOG & ~0x40000000u) && caller_matches(host))))
            return refuse(host, "REL resident profile prolog requires its completed fixed-link sequence and exact caller");
        if (block->phase == WW_REL_PROFILE_ACTIVE)
            return NULL;
        if (fprintf(stderr, "[rel-profile-prolog] stage=entered pc=%08X owner=%08X certified=0\n", address, block->owner) < 0)
            return refuse(host, "REL resident profile prolog trace failed");
        host->rel_profile_block.r1=host->cpu.gpr[1]; host->rel_profile_block.r2=host->cpu.gpr[2];
        host->rel_profile_block.r13=host->cpu.gpr[13]; host->rel_profile_block.r31=host->cpu.gpr[31];
        block->phase=WW_REL_PROFILE_ACTIVE; block->window=PROFILE_PROLOG & ~0x40000000u;
    } else if (address == PROFILE_RETURN && host->rel_pending_init.phase == WW_REL_INIT_NONE) {
        if (ww_host_rel_profile_return_qualified(host, address))
            return NULL;
        if (block->phase != WW_REL_PROFILE_CONSTRUCTED || host->cpu.exception != 0u || !caller_matches(host) ||
            host->cpu.pc != address || host->rel_constructor_owner != 0u || !intact(host, block))
            return refuse(host, "REL resident profile return requires its actual prolog and constructor completion");
        if (fprintf(stderr, "[rel-profile-prolog] stage=caller-returned pc=%08X owner=%08X certified=0\n", address, block->owner) < 0)
            return refuse(host, "REL resident profile completion trace failed");
        block->image->initialization_completed=true;
        block->phase=WW_REL_PROFILE_COMPLETE; block->window=PROFILE_RETURN;
    }
    return NULL;
}

const char* ww_host_rel_profile_constructor(WwHost* host, const WwRelImage* image,
                                           u32 section, u32 offset, u32 caller, bool complete) {
    WwRelBlock* block = &host->rel_profile_block.allocation;
    if (!ww_host_rel_profile_enabled(host) || image->module_id != 1u)
        return NULL;
    if (image != block->image || !intact(host, block) || host->cpu.exception != 0u || block->phase != WW_REL_PROFILE_ACTIVE ||
        host->rel_pending_init.phase != WW_REL_INIT_NONE || caller != PROFILE_CTOR_RETURN ||
        (!complete && (section != 2u || offset != 0u ||
         (ww_host_rel_address(host, host->cpu.pc) & ~0x40000000u) != 0x80241178u)) ||
        (complete && (host->rel_constructor_owner != block->owner ||
         ww_host_rel_address(host, host->cpu.pc) != PROFILE_CTOR_RETURN)))
        return refuse(host, "REL resident profile constructors require their actual active prolog and exact table/successor");
    block->window = 0u;
    if (complete) {
        if (fprintf(stderr, "[rel-profile-prolog] stage=ctor-completed pc=%08X owner=%08X certified=0\n", caller, block->owner) < 0)
            return refuse(host, "REL resident profile constructor completion trace failed");
        block->phase = WW_REL_PROFILE_CONSTRUCTED;
    }
    return NULL;
}

bool ww_host_rel_profile_completed(WwHost* host, const WwRelImage* image) {
    const WwRelBlock* block = &host->rel_profile_block.allocation;
    return ww_host_rel_profile_enabled(host) && host->error == NULL && block->phase == WW_REL_PROFILE_COMPLETE &&
        image == block->image && image->guest == block->guest && image->certificate == NULL &&
        image->initialization_completed && ww_host_rel_raw_unchanged(image);
}

const char* ww_host_rel_title_expose(WwHost* host, WwRelImage* image) {
    if (!ww_host_rel_profile_enabled(host) || image == NULL || image->module_id != 400u)
        return NULL;
    WwRelBlock* block = &host->rel_title_block;
    const BlueWakeRelInitContractV1* certificate = ww_host_rel_certificate(host, 400u);
    if (certificate == NULL || image->certificate != certificate || image->bytes != certificate->rel_size)
        return block_refuse(block, "REL title allocation requires its exact admitted certificate");
    const char* error = expose(host, image, block, certificate->rel_bytes);
    if (error != NULL)
        return error;
    if (fprintf(stderr, "[rel-title-block] stage=exposed module=400 guest=%08X owner=%08X bytes=%u capacity=%u logical=%u raw_fix=%u extent=%u certified=0\n",
        block->guest, block->owner, block->bytes, block->capacity, block->logical_size, block->raw_fix, block->extent) < 0)
        return block_refuse(block, "REL title allocation exposure trace failed");
    return NULL;
}

bool ww_host_rel_title_inputs(WwHost* host, const WwRelImage* image) {
    const WwRelBlock* block = &host->rel_title_block;
    return image == block->image && identity(host, block) && image->sdk_fixed &&
        image->sdk_input_form == WW_REL_SDK_TITLE_INLINE_ZERO && image->sdk_bss == block->guest + block->extent;
}

#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
static const char* inline_entry(WwHost* host, WwRelImage* image, bool fixed, WwRelBlock* block, bool title) {
    if (block == NULL)
        return "REL inline entry requires its policy-bound exposed allocation";
#else
const char* ww_host_rel_title_entry(WwHost* host, WwRelImage* image, bool fixed) {
    WwRelBlock* block = &host->rel_title_block;
#endif
    const CPUState* cpu = &host->cpu;
    WwRelImage* profile = ww_host_rel_live(host, 1u);
    WwRelImage* ship = ww_host_rel_live(host, 168u);
    if (!fixed || image != block->image || !original(host, block) || cpu->pc != SDK_FIXED ||
        cpu->lr != SDK_RETURN || cpu->exception != 0u || cpu->gpr[3] != block->guest ||
        cpu->gpr[4] != block->guest + block->extent || cpu->gpr[31] != block->owner ||
        !ww_host_module_contains_address(host->module, SDK_FIXED) || !ww_host_module_contains_address(host->module, SDK_RETURN) ||
        !(block->phase == WW_REL_PROFILE_SIZE || (block->phase == WW_REL_PROFILE_SDK && block->window == SDK_FIXED)) ||
        profile == NULL || profile != host->rel_profile_block.allocation.image ||
        host->rel_profile_block.allocation.phase != WW_REL_PROFILE_COMPLETE || !profile->initialization_completed ||
        profile->certificate != NULL || profile->sdk_phase != WW_REL_SDK_RESIDENT ||
        ship == NULL || !ship->initialization_completed || ship->sdk_phase != WW_REL_SDK_CERTIFIED ||
        ship->certificate != ww_host_rel_certificate(host, 168u) || ship->sdk_fixed ||
        ship->sdk_previous != profile->guest || profile->sdk_next != ship->guest ||
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        host->rel_sdk_head != profile->guest || (title && host->rel_sdk_tail != ship->guest) || host->rel_sdk_failed ||
#else
        host->rel_sdk_head != profile->guest || host->rel_sdk_tail != ship->guest || host->rel_sdk_failed ||
#endif
        (host->rel_sdk_pending_owner != 0u && host->rel_sdk_pending_owner != image->owner))
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        return block_refuse(block, title ?
            "REL title requires its exact observed inline fixed SDK entry after completed profile and ship" :
            "REL actor requires its exact observed inline fixed SDK entry after completed profile and ship");
#else
        return block_refuse(block, "REL title requires its exact observed inline fixed SDK entry after completed profile and ship");
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!title) {
        WwRelImage* old_title = ww_host_rel_live(host, 400u);
        WwRelImage* ikada = ww_host_rel_live(host, 60u);
        if (!host->rel_sdk_seeded || host->rel_pending_init.phase != WW_REL_INIT_NONE || host->rel_constructor_owner != 0u ||
            old_title == NULL || ikada == NULL || !old_title->initialization_completed || !ikada->initialization_completed ||
            old_title->sdk_phase != WW_REL_SDK_CERTIFIED || ikada->sdk_phase != WW_REL_SDK_CERTIFIED ||
            old_title->certificate != ww_host_rel_certificate(host, 400u) || ikada->certificate != ww_host_rel_certificate(host, 60u) ||
            old_title->sdk_previous != ship->guest || ship->sdk_next != old_title->guest ||
            ikada->sdk_previous != old_title->guest || old_title->sdk_next != ikada->guest)
            return block_refuse(block, "REL actor inline entry requires the completed existing initialization chain");
        for (u32 at = 0u; at < host->rel_image_count; ++at) {
            const WwRelImage* other = &host->rel_images[at];
            if (other != image && other->sdk_phase == WW_REL_SDK_CERTIFIED && !other->initialization_completed)
                return block_refuse(block, "REL actor inline entry cannot overlap an incomplete certified initialization");
        }
    }
#endif
    return NULL;
}

#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
const char* ww_host_rel_title_entry(WwHost* host, WwRelImage* image, bool fixed) {
    return inline_entry(host, image, fixed, &host->rel_title_block, true);
}

const char* ww_host_rel_actor_entry(WwHost* host, WwRelImage* image, bool fixed) {
    return inline_entry(host, image, fixed, ww_host_rel_actor_block(host, image), false);
}

static const char* inline_return(WwHost* host, WwRelBlock* block, bool inputs) {
    if (block == NULL)
        return "REL inline return requires its policy-bound allocation";
#else
const char* ww_host_rel_title_return(WwHost* host, const WwRelImage* image) {
    WwRelBlock* block = &host->rel_title_block;
#endif
    const CPUState* cpu = &host->cpu;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (!inputs || cpu->pc != SDK_RETURN || cpu->lr != SDK_RETURN ||
#else
    if (!ww_host_rel_title_inputs(host, image) || cpu->pc != SDK_RETURN || cpu->lr != SDK_RETURN ||
#endif
        cpu->exception != 0u || cpu->gpr[3] != 1u || cpu->gpr[31] != block->owner ||
        !ww_host_module_contains_address(host->module, SDK_RETURN) ||
        !(block->phase == WW_REL_PROFILE_SDK ||
          (block->phase == WW_REL_PROFILE_RETURNED && block->window == SDK_RETURN)))
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        return block_refuse(block, "REL inline SDK certification requires its actual successful fixed return");
#else
        return block_refuse(block, "REL title SDK certification requires its actual successful fixed return");
#endif
    return NULL;
}

#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
const char* ww_host_rel_title_return(WwHost* host, const WwRelImage* image) {
    return inline_return(host, &host->rel_title_block, ww_host_rel_title_inputs(host, image));
}

const char* ww_host_rel_actor_return(WwHost* host, const WwRelImage* image) {
    return inline_return(host, ww_host_rel_actor_block(host, image), ww_host_rel_actor_inputs(host, image));
}

const char* ww_host_rel_actor_expose(WwHost* host, WwRelImage* image) {
    if (ww_host_actor_sdk_contract(host, image->module_id) != WW_ACTOR_SDK_CERTIFIED_INLINE_ZERO)
        return NULL;
    WwRelBlock* block = ww_host_rel_actor_block(host, image);
    if (block == NULL || block->phase != WW_REL_PROFILE_NONE)
        return "REL actor allocation requires unique unexposed policy bookkeeping";
    const char* error = expose(host, image, block, block->certificate->rel_bytes);
    if (error != NULL)
        return error;
    if (fprintf(stderr, "[rel-actor-block] stage=exposed module=%u guest=%08X owner=%08X bytes=%u capacity=%u logical=%u raw_fix=%u extent=%u expected_bss=%08X owner_bss=00000000 certificate=%s policy=%s certified=0\n",
        image->module_id, block->guest, block->owner, block->bytes, block->capacity, block->logical_size,
        block->raw_fix, block->extent, block->guest + block->extent, block->certificate->build_identity,
        host->rel_actor_policy_identity) < 0 || fflush(stderr) != 0)
        return block_refuse(block, "REL actor allocation exposure trace write/flush failed");
    return NULL;
}

bool ww_host_rel_actor_published(WwHost* host, const WwRelImage* image) {
    const WwRelBlock* block = ww_host_rel_actor_block(host, image);
    return block != NULL && block->image == image && original(host, block);
}

bool ww_host_rel_actor_inputs(WwHost* host, const WwRelImage* image) {
    const WwRelBlock* block = ww_host_rel_actor_block(host, image);
    return block != NULL && block->image == image && identity(host, block) && image->sdk_fixed &&
        image->sdk_input_form == WW_REL_SDK_CERTIFIED_INLINE_ZERO && image->sdk_bss == block->guest + block->extent;
}

bool ww_host_rel_actor_accounted(WwHost* host, const WwRelImage* image) {
    const WwRelBlock* block = ww_host_rel_actor_block(host, image);
    return block != NULL && block->image == image && block->phase == WW_REL_PROFILE_SHRUNK &&
        block->accounting_observed && block->logical_size == block->extent && intact(host, block);
}

void ww_host_rel_actor_failed(WwHost* host, const WwRelImage* image) {
    WwRelBlock* block = actor_record(host, image);
    if (block != NULL)
        block_refuse(block, "REL actor inline allocation sequence failed");
}

void ww_host_rel_actor_entered(WwRelBlock* block) {
    block->phase = WW_REL_PROFILE_SDK;
    block->window = SDK_FIXED;
}

void ww_host_rel_actor_returned(WwRelBlock* block) {
    block->phase = WW_REL_PROFILE_RETURNED;
    block->window = SDK_RETURN;
}

const char* ww_host_rel_actor_observe(WwHost* host, u32 address, bool* handled) {
    *handled = false;
    WwRelBlock* selected = NULL;
    const bool entry = address == SDK_FIXED || address == 0x80305424u;
    const bool returning = address == SDK_RETURN;
    for (u32 at = 0u; at < host->rel_actor_block_count; ++at) {
        WwRelBlock* block = &host->rel_actor_blocks[at];
        WwRelImage* image = block->image;
        if (image == NULL && block->certificate != NULL)
            image = ww_host_rel_live(host, block->certificate->module_id);
        if (image == NULL)
            continue;
        if (address != block->window)
            block->window = 0u;
        const u32 pointer = host->cpu.gpr[3] & ~0x40000000u;
        const bool outstanding = block->phase == WW_REL_PROFILE_SIZE || block->phase == WW_REL_PROFILE_SDK;
        const bool exposed = block->image != NULL;
        const u32 base = exposed ? block->guest : image->guest;
        const u32 capacity = exposed ? block->capacity : image->capacity;
        const bool associated = host->cpu.gpr[31] == (exposed ? block->owner : image->owner) ||
            (entry && pointer >= base && (u64)pointer < (u64)base + capacity);
        if (!(entry || returning) || !(associated || outstanding))
            continue;
        *handled = true;
        if (selected != NULL && selected != block) {
            block_refuse(selected, "REL actor SDK observation has conflicting allocations");
            return block_refuse(block, "REL actor SDK observation has conflicting allocations");
        }
        selected = block;
        if (block->image == NULL)
            return block_refuse(block, "REL actor SDK observation requires its first successful allocation exposure");
    }
    if (selected == NULL)
        return NULL;
    if (returning) {
        const char* error = ww_host_rel_actor_return(host, selected->image);
        return error != NULL ? block_refuse(selected, error) : ww_host_rel_sdk_finish(host, selected->image);
    }
    return ww_host_rel_sdk_prepare(host, selected->image, address == SDK_FIXED);
}
#endif

void ww_host_rel_title_entered(WwHost* host) {
    host->rel_title_block.phase = WW_REL_PROFILE_SDK;
    host->rel_title_block.window = SDK_FIXED;
}

void ww_host_rel_title_returned(WwHost* host) {
    host->rel_title_block.phase = WW_REL_PROFILE_RETURNED;
    host->rel_title_block.window = SDK_RETURN;
}

bool ww_host_rel_title_accounted(WwHost* host, const WwRelImage* image) {
    const WwRelBlock* block = &host->rel_title_block;
    return image == block->image && block->phase == WW_REL_PROFILE_SHRUNK && block->accounting_observed &&
        block->logical_size == block->extent && intact(host, block);
}

bool ww_host_rel_title_published(WwHost* host, const WwRelImage* image) {
    return image == host->rel_title_block.image && original(host, &host->rel_title_block);
}

void ww_host_rel_title_failed(WwHost* host) {
    block_refuse(&host->rel_title_block, "REL title allocation sequence failed");
}
