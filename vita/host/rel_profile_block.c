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
}

static const u8* block_raw(const WwHost* host, const WwRelBlock* block) {
    return block == &host->rel_profile_block.allocation ? host->rel_profile_block.raw : block->identity;
}

static bool identity(WwHost* host, const WwRelBlock* block) {
    const WwRelImage* image = block->image;
    const u8* raw = block_raw(host, block);
    const u32 module = block == &host->rel_profile_block.allocation ? 1u : 400u;
    u8* live = NULL;
    if (!ww_host_rel_profile_enabled(host) || host->error != NULL || image == NULL || raw == NULL ||
        block->phase == WW_REL_PROFILE_FAILED || image != ww_host_rel_owned(host, block->owner) ||
        image != ww_host_rel_live(host, module) || image->module_id != module || !image->linked || image->unlinked ||
        (module == 1u ? image->certificate != NULL :
         (block->certificate == NULL || image->certificate != block->certificate ||
          block->certificate != ww_host_rel_certificate(host, 400u) ||
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
    if (module == 400u && (bluewake_archive_be32(raw + 0x1Cu) != 3u ||
        bluewake_archive_be32(raw + 0x20u) != 0u || bluewake_archive_be32(raw + 0x48u) != block->raw_fix ||
        block->extent != (((u64)block->raw_fix + 31u) & ~31ull) ||
        block->raw_fix < 0x4Cu || block->extent >= block->bytes || block->bytes > block->capacity ||
        (u64)block->extent + 16u >= block->capacity || block->guest < WW_REL_SCRATCH_BASE ||
        (u64)block->guest + block->capacity > WW_REL_SCRATCH_LIMIT))
        return false;
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
    const bool title = block == &host->rel_title_block;
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
    }
    *block = (WwRelBlock){.phase=WW_REL_PROFILE_EXPOSED,.image=image,.sections=image->sections,
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
        (ww_host_rel_raw_unchanged(block->image) || (block == &host->rel_title_block &&
         block->image->sdk_phase == WW_REL_SDK_PENDING && block->image->raw_hash == ww_host_rel_raw_hash(block_raw(host, block), block->bytes))) &&
        ppc_guest_alias_get_storage(block->guest, block->capacity, &live) &&
        memcmp(live, block_raw(host, block), block->bytes) == 0;
}

static bool intact(WwHost* host, const WwRelBlock* block) {
    return identity(host, block) && ww_host_rel_sections_match_post_link(block->image) &&
        ww_host_rel_raw_unchanged(block->image) &&
        (block == &host->rel_title_block ? ww_host_rel_sdk_integrity(host) :
         mem_read32(&host->cpu, 0x800030D0u) == host->rel_profile_block.name_base);
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
    const bool initial = address == BLOCK_SIZE && cpu->lr == 0x80240CC4u;
    const bool accounting = address == BLOCK_SIZE && cpu->lr == 0x80241058u;
    const bool shrink = address == BLOCK_SHRINK && cpu->lr == 0x80240D80u;
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
    if (error == NULL && block->window != address && fprintf(stderr, format,
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

const char* ww_host_rel_title_entry(WwHost* host, WwRelImage* image, bool fixed) {
    WwRelBlock* block = &host->rel_title_block;
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
        host->rel_sdk_head != profile->guest || host->rel_sdk_tail != ship->guest || host->rel_sdk_failed ||
        (host->rel_sdk_pending_owner != 0u && host->rel_sdk_pending_owner != image->owner))
        return block_refuse(block, "REL title requires its exact observed inline fixed SDK entry after completed profile and ship");
    return NULL;
}

const char* ww_host_rel_title_return(WwHost* host, const WwRelImage* image) {
    WwRelBlock* block = &host->rel_title_block;
    const CPUState* cpu = &host->cpu;
    if (!ww_host_rel_title_inputs(host, image) || cpu->pc != SDK_RETURN || cpu->lr != SDK_RETURN ||
        cpu->exception != 0u || cpu->gpr[3] != 1u || cpu->gpr[31] != block->owner ||
        !ww_host_module_contains_address(host->module, SDK_RETURN) ||
        !(block->phase == WW_REL_PROFILE_SDK ||
          (block->phase == WW_REL_PROFILE_RETURNED && block->window == SDK_RETURN)))
        return block_refuse(block, "REL title SDK certification requires its actual successful fixed return");
    return NULL;
}

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
