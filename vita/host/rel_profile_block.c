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

static const char* refuse(WwHost* host, const char* error) {
    host->rel_profile_block.phase = WW_REL_PROFILE_FAILED;
    host->rel_profile_block.window = 0u;
    return error;
}

void ww_host_rel_profile_revoke(WwHost* host) {
    WwRelProfileBlock* block = &host->rel_profile_block;
    if (block->phase != WW_REL_PROFILE_NONE && block->phase != WW_REL_PROFILE_COMPLETE)
        refuse(host, "REL resident profile completion revoked");
}

static bool identity(WwHost* host) {
    const WwRelProfileBlock* block = &host->rel_profile_block;
    const WwRelImage* image = block->image;
    u8* live = NULL;
    if (!ww_host_rel_profile_enabled(host) || host->error != NULL || image == NULL || block->raw == NULL ||
        block->phase == WW_REL_PROFILE_FAILED || image != ww_host_rel_owned(host, block->owner) ||
        image != ww_host_rel_live(host, 1u) || image->certificate != NULL || image->module_id != 1u ||
        image->owner != block->owner || image->guest != block->guest || image->bytes != block->bytes ||
        image->capacity != block->capacity || image->sections != block->sections ||
        mem_read32(&host->cpu, block->owner + 0x10u) != block->guest ||
        mem_read32(&host->cpu, block->owner + 0x14u) != 0u ||
        mem_read32(&host->cpu, BLUEWAKE_PROFILE_LIST_GLOBAL) != block->publication ||
        !ppc_guest_alias_get_storage(block->guest, block->capacity, &live))
        return false;
    const u32 count = bluewake_archive_be32(block->raw + 0xCu);
    const u32 table = bluewake_archive_be32(block->raw + 0x10u);
    if (image->section_count != count || image->section_table != table)
        return false;
    for (u32 at = 0u; at < host->rel_image_count; ++at) {
        const WwRelImage* other = &host->rel_images[at];
        if (other != image && other->guest != 0u && (other->module_id == 1u ||
            ((u64)other->guest < (u64)block->guest + block->capacity &&
             (u64)block->guest < (u64)other->guest + other->capacity)))
            return false;
    }
    const StaticRecompRelModule* rel = ww_host_rel_descriptor(host, 1u);
    if (rel == NULL)
        return false;
    for (u32 at = 0u; at < count; ++at) {
        const WwRelSection* section = &image->sections[at];
        const StaticRecompRelSection* compiled = ww_rel_section(rel, at);
        if (section->flags != bluewake_archive_be32(block->raw + table + at * 8u) ||
            section->size != bluewake_archive_be32(block->raw + table + at * 8u + 4u) ||
            section->linked_start != (compiled == NULL ? 0u : compiled->linked_start))
            return false;
    }
    return true;
}

const char* ww_host_rel_profile_expose(WwHost* host, WwRelImage* image) {
    if (!ww_host_rel_profile_enabled(host))
        return NULL;
    if (host->error != NULL)
        return refuse(host, host->error);
    WwRelProfileBlock* block = &host->rel_profile_block;
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
    const u64 extent = ((u64)bluewake_archive_be32(raw + 0x48u) + 31u) & ~31ull;
    const u32 align = bluewake_archive_be32(raw + 0x40u);
    const u32 bss_align = bluewake_archive_be32(raw + 0x44u);
    if (extent < 0x4Cu || extent > image->bytes || extent + 16u >= image->capacity ||
        (u64)image->guest + image->capacity > WW_REL_MODULE_REGION_LIMIT ||
        (align != 0u && image->guest % align != 0u) ||
        (bss_align != 0u && (image->guest + extent) % bss_align != 0u) ||
        raw[0x30u] >= image->section_count || raw[0x30u] == 0u ||
        image->sections[raw[0x30u]].linked_start + bluewake_archive_be32(raw + 0x34u) != PROFILE_PROLOG)
        return refuse(host, "REL resident profile allocation has an invalid bounded resident extent or prolog");
    u8* frozen = malloc(image->bytes);
    if (frozen == NULL)
        return refuse(host, "REL resident profile verification allocation failed");
    memcpy(frozen, raw, image->bytes);
    *block = (WwRelProfileBlock){.phase=WW_REL_PROFILE_EXPOSED,.image=image,.sections=image->sections,.raw=frozen,
        .owner=image->owner,.guest=image->guest,.bytes=image->bytes,.capacity=image->capacity,
        .extent=(u32)extent,.logical_size=image->capacity,.publication=profile->linked_start};
    if (!identity(host))
        return refuse(host, "REL resident profile allocation ownership is inconsistent");
    if (fprintf(stderr, "[rel-profile-block] stage=exposed guest=%08X owner=%08X bytes=%u capacity=%u logical=%u extent=%u certified=0\n",
                block->guest, block->owner, block->bytes, block->capacity, block->logical_size, block->extent) < 0)
        return refuse(host, "REL resident profile allocation trace failed");
    return NULL;
}

static bool original(WwHost* host) {
    const WwRelProfileBlock* block = &host->rel_profile_block;
    u8* live = NULL;
    return identity(host) && ww_host_rel_sections_match(block->image) &&
        ww_host_rel_raw_unchanged(block->image) &&
        ppc_guest_alias_get_storage(block->guest, block->capacity, &live) &&
        memcmp(live, block->raw, block->bytes) == 0;
}

static bool intact(WwHost* host) {
    const WwRelProfileBlock* block = &host->rel_profile_block;
    return identity(host) && ww_host_rel_sections_match_post_link(block->image) &&
        ww_host_rel_raw_unchanged(block->image) &&
        mem_read32(&host->cpu, 0x800030D0u) == block->name_base;
}

const char* ww_host_rel_profile_observe(WwHost* host, u32 address) {
    WwRelProfileBlock* block = &host->rel_profile_block;
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
            !ww_host_module_contains_address(host->module, SDK_RETURN) || !original(host) ||
            !((block->phase == WW_REL_PROFILE_SIZE) ||
              (block->phase == WW_REL_PROFILE_SDK && block->window == SDK_FIXED)) ||
            host->rel_sdk_seeded || host->rel_sdk_pending_owner != 0u || host->rel_sdk_failed ||
            mem_read32(cpu, 0x800030C8u) != 0u || mem_read32(cpu, 0x800030CCu) != 0u)
            return refuse(host, "REL resident profile requires its exact first fixed SDK entry");
        const u32 names = mem_read32(cpu, 0x800030D0u);
        const u64 name = (u64)names + bluewake_archive_be32(block->raw + 0x14u);
        const u32 name_size = bluewake_archive_be32(block->raw + 0x18u);
        if (name > UINT32_MAX || (name != 0u && (name < GC_RAM_BASE || name >= GC_RAM_BASE + GC_MAIN_RAM_SIZE)) ||
            (name_size != 0u && (name < GC_RAM_BASE || name + name_size > GC_RAM_BASE + GC_MAIN_RAM_SIZE ||
             ww_host_linked_alias_overlaps((u32)name, name_size))) ||
            (block->phase == WW_REL_PROFILE_SDK && block->name_base != names))
            return refuse(host, "REL resident profile SDK string-table input changed or is unbounded");
        if (block->phase != WW_REL_PROFILE_SDK &&
            fprintf(stderr, "[rel-profile-sdk] stage=entered pc=%08X module=%08X bss=%08X caller=%08X certified=0\n",
                    address, cpu->gpr[3], cpu->gpr[4], cpu->lr) < 0)
            return refuse(host, "REL resident profile SDK entry trace failed");
        block->name_base = names;
        block->phase = WW_REL_PROFILE_SDK;
        block->window = SDK_FIXED;
    } else if (address == SDK_RETURN && (block->phase == WW_REL_PROFILE_SDK ||
               (block->phase == WW_REL_PROFILE_RETURNED && block->window == SDK_RETURN))) {
        if (cpu->pc != address || cpu->exception != 0u || cpu->gpr[3] != 1u || cpu->lr != SDK_RETURN ||
            cpu->gpr[31] != block->owner || !identity(host) ||
            mem_read32(cpu, 0x800030D0u) != block->name_base ||
            mem_read32(cpu, 0x800030C8u) != block->guest || mem_read32(cpu, 0x800030CCu) != block->guest)
            return refuse(host, "REL resident profile SDK return requires its successful guest singleton");
        if (block->phase == WW_REL_PROFILE_RETURNED)
            return intact(host) ? NULL : refuse(host, "REL resident profile SDK duplicate return changed");
        u8* expected = malloc(block->bytes);
        if (expected == NULL)
            return refuse(host, "REL resident profile SDK reconstruction allocation failed");
        memcpy(expected, block->raw, block->bytes);
        WwRelImage sdk = *block->image;
        sdk.sdk_fixed = true;
        sdk.sdk_previous = 0u;
        sdk.sdk_bss = block->guest + block->extent;
        sdk.sdk_name_base = block->name_base;
        const char* error = ww_rel_replay_resident(host, ww_host_rel_descriptor(host, 1u),
            block->raw, block->bytes, expected, &sdk);
        if (error == NULL)
            error = ww_rel_sdk_structure(&sdk, block->raw, expected, 0u);
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

WwRelProfileService ww_host_rel_profile_service(WwHost* host, u32 address) {
    WwRelProfileBlock* block = &host->rel_profile_block;
    CPUState* cpu = &host->cpu;
    if (!ww_host_rel_profile_enabled(host) || block->phase == WW_REL_PROFILE_NONE ||
        (address != BLOCK_SIZE && address != BLOCK_SHRINK) || cpu->exception != 0u ||
        cpu->gpr[3] < block->guest || (u64)cpu->gpr[3] >= (u64)block->guest + block->capacity)
        return WW_REL_PROFILE_MISSED;
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
        (initial ? original(host) : intact(host)) &&
        (shrink ? cpu->gpr[4] == block->extent && cpu->gpr[5] == 0u : cpu->gpr[4] == 0u);
    const char* error = valid ? NULL : "REL resident profile block operation has inconsistent ownership, arguments or sequence";
    if (error == NULL && block->window != address && fprintf(stderr,
        "[rel-profile-block] stage=%s guest=%08X owner=%08X logical=%u capacity=%u entry=%08X caller=%08X certified=0\n",
        initial ? "size" : shrink ? "shrunk" : "accounting", block->guest, block->owner,
        shrink ? block->extent : block->logical_size, block->capacity, address, cpu->lr) < 0)
        error = "REL resident profile block operation trace failed";
    if (error != NULL) {
        ww_host_fail(host, refuse(host, error), address, cpu->pc);
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
    const WwRelProfileBlock* block = &host->rel_profile_block;
    const CPUState* cpu = &host->cpu;
    return cpu->gpr[1] == block->r1 && cpu->gpr[2] == block->r2 && cpu->gpr[13] == block->r13 &&
        cpu->gpr[31] == block->r31 && block->r31 == block->owner && cpu->lr == PROFILE_RETURN;
}

bool ww_host_rel_profile_return_qualified(WwHost* host, u32 address) {
    const WwRelProfileBlock* block = &host->rel_profile_block;
    return block->phase == WW_REL_PROFILE_COMPLETE && block->window == PROFILE_RETURN &&
        address == PROFILE_RETURN && host->cpu.pc == address && host->cpu.exception == 0u && caller_matches(host) && intact(host) &&
        host->rel_pending_init.phase == WW_REL_INIT_NONE && host->rel_constructor_owner == 0u;
}

const char* ww_host_rel_profile_lifecycle(WwHost* host, u32 address) {
    WwRelProfileBlock* block = &host->rel_profile_block;
    if (!ww_host_rel_profile_enabled(host) || block->phase == WW_REL_PROFILE_NONE)
        return NULL;
    if ((address & ~0x40000000u) != block->window)
        block->window = 0u;
    if (address == PROFILE_PROLOG) {
        if (!intact(host) || host->cpu.exception != 0u || host->rel_pending_init.phase != WW_REL_INIT_NONE ||
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
        block->r1=host->cpu.gpr[1]; block->r2=host->cpu.gpr[2];
        block->r13=host->cpu.gpr[13]; block->r31=host->cpu.gpr[31];
        block->phase=WW_REL_PROFILE_ACTIVE; block->window=PROFILE_PROLOG & ~0x40000000u;
    } else if (address == PROFILE_RETURN && host->rel_pending_init.phase == WW_REL_INIT_NONE) {
        if (ww_host_rel_profile_return_qualified(host, address))
            return NULL;
        if (block->phase != WW_REL_PROFILE_CONSTRUCTED || host->cpu.exception != 0u || !caller_matches(host) ||
            host->cpu.pc != address || host->rel_constructor_owner != 0u || !intact(host))
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
    WwRelProfileBlock* block = &host->rel_profile_block;
    if (!ww_host_rel_profile_enabled(host) || image->module_id != 1u)
        return NULL;
    if (image != block->image || !intact(host) || host->cpu.exception != 0u || block->phase != WW_REL_PROFILE_ACTIVE ||
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
    const WwRelProfileBlock* block = &host->rel_profile_block;
    return ww_host_rel_profile_enabled(host) && host->error == NULL && block->phase == WW_REL_PROFILE_COMPLETE &&
        image == block->image && image->guest == block->guest && image->certificate == NULL &&
        image->initialization_completed && ww_host_rel_raw_unchanged(image);
}
