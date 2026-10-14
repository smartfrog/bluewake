#include "boot_host.h"
#include "rel_archive.h"

const char* ww_host_enable_ship_contract(WwHost* host, const BlueWakeShipContractV1* contract, const char* dol) {
    if (host == NULL || host->ship_contract != NULL || contract == NULL || contract->version != 1u ||
        contract->dol_bytes == NULL || contract->dol_size == 0u || contract->dol_size > 0x800000u ||
        contract->rel_bytes == NULL || contract->rel_size < 0x4Cu || contract->rel_size > 0x100000u ||
        contract->canonical_sections == NULL || contract->section_count == 0u || contract->section_count > 256u ||
        contract->build_identity == NULL || strnlen(contract->build_identity, 65u) != 64u ||
        strspn(contract->build_identity, "0123456789abcdef") != 64u || dol == NULL)
        return "ship prolog requires a complete generated exact build contract";
    const char* error = ww_rel_check_dol(contract->dol_bytes, contract->dol_size, dol);
    host->ship_lifecycle = (BlueWakeRelLifecycleV2){168u, 0xC1B100F4u, 0xC1B10120u, 0xC1B1014Cu};
    BlueWakeRelInitContractV1 certificate = {.module_id = 168u, .rel_size = contract->rel_size,
        .rel_bytes = contract->rel_bytes, .canonical_sections = contract->canonical_sections,
        .section_count = contract->section_count, .ctor_return_pc = 0xC1B1010Cu,
        .lifecycle = &host->ship_lifecycle, .build_identity = contract->build_identity};
    for (u32 at = 0u; at < contract->section_count; ++at) {
        const BlueWakeRelData* section = &contract->canonical_sections[at];
        if (section->linked_start <= 0xC1B1DAF0u &&
            (u64)0xC1B1DAF0u + 4u <= (u64)section->linked_start + section->size) {
            certificate.ctor_table_section = section->section_index;
            certificate.ctor_table_offset = 0xC1B1DAF0u - section->linked_start;
        }
    }
    if (error == NULL)
        error = ww_rel_validate_certificate(host, &certificate);
    if (error == NULL) {
        host->ship_certificate = certificate;
        host->ship_contract = contract;
    }
    return error;
}

static void clear_ship_return(WwHost* host) {
    host->ship_prolog_owner = 0u;
    host->ship_prolog_stack = 0u;
    host->ship_prolog_r2 = 0u;
    host->ship_prolog_r13 = 0u;
    host->ship_prolog_ctor_completed = false;
    host->ship_prolog_return_ready = false;
    host->ship_prolog_return_interrupted = false;
}

const char* ww_host_ship_exception_restored(WwHost* host, u32 address) {
    if (!host->ship_prolog_return_interrupted)
        return NULL;
    if (address != 0x80240EE8u) {
        clear_ship_return(host);
        return "ship prolog exception restoration changed the pending caller PC";
    }
    host->ship_prolog_return_interrupted = false;
    const char* error = ww_host_ship_boundary(host, address);
    if (error != NULL)
        clear_ship_return(host);
    return error;
}

const char* ww_host_ship_boundary(WwHost* host, u32 address) {
    const u32 canonical = address & ~0x40000000u;
    if (canonical == 0x81B100F4u && host->ship_prolog_return_interrupted) {
        clear_ship_return(host);
        return "ship prolog entry is forbidden while its completed return is suspended";
    }
    if (canonical == 0x80240EE8u && host->ship_prolog_return_interrupted) {
        clear_ship_return(host);
        return "ship prolog return requires its verified exception restoration";
    }
    if (host->ship_prolog_return_ready && canonical != 0x80240EE8u &&
        !host->ship_prolog_return_interrupted)
        clear_ship_return(host);
    if (canonical == 0x81B10120u || canonical == 0x81B1014Cu)
        return "ship epilog or unresolved entry has no supported lifecycle contract";
    if (host->ship_prolog_owner != 0u) {
        const WwRelImage* image = ww_host_rel_live(host, 168u);
        if (image == NULL || image->owner != host->ship_prolog_owner || !ww_host_rel_raw_unchanged(image) ||
            (image->sdk_phase == WW_REL_SDK_CERTIFIED && !ww_host_rel_sdk_integrity(host)))
            return "ship prolog continuation lost its exact live owner";
        if (canonical == 0x80240EE8u) {
            if (host->cpu.lr != 0x80240EE8u || host->cpu.gpr[1] != host->ship_prolog_stack ||
                host->cpu.gpr[31] != image->owner || host->rel_constructor_owner != 0u ||
                host->cpu.gpr[2] != host->ship_prolog_r2 || host->cpu.gpr[13] != host->ship_prolog_r13 ||
                !host->ship_prolog_ctor_completed)
                return "ship prolog return did not preserve its translated caller";
            if (!host->ship_prolog_return_ready &&
                fprintf(stderr, "[rel-prolog] module=168 stage=caller-returned pc=%08X owner=%08X\n", address, image->owner) < 0)
                return "ship prolog return trace failed";
            host->ship_prolog_return_ready = true;
        }
    }
    if (canonical == 0x80240EE8u && host->ship_contract != NULL &&
        ww_host_rel_live(host, 168u) != NULL && !host->ship_prolog_return_ready)
        return "ship prolog return requires its completed translated call";
    if (canonical != 0x81B100F4u)
        return NULL;
    WwRelImage* image = ww_host_rel_live(host, 168u);
    u64 entry_hash = image == NULL ? 0u : image->raw_hash;
    if (host->ship_contract == NULL || image == NULL ||
        host->cpu.lr != 0x80240EE8u || host->cpu.gpr[31] != image->owner ||
        host->cpu.gpr[3] != image->guest ||
        (image->sdk_phase == WW_REL_SDK_CERTIFIED && !ww_host_rel_sdk_integrity(host)) ||
        (ww_host_rel_address(host, host->cpu.pc) & ~0x40000000u) != canonical ||
        (!ww_host_rel_raw_unchanged(image) &&
         (host->ship_prolog_owner != 0u || !ww_host_ship_sdk_hash(host, image, &entry_hash))))
        return "ship prolog entry requires its exact translated dynamic-link caller";
    if (host->ship_prolog_owner != 0u)
        return host->ship_prolog_owner == image->owner && host->ship_prolog_stack == host->cpu.gpr[1] ?
            NULL : "ship prolog does not support a nested entry";
    if (fprintf(stderr, "[rel-prolog] module=168 stage=entered pc=%08X owner=%08X lr=%08X\n", address, image->owner, host->cpu.lr) < 0)
        return "ship prolog entry trace failed";
    // Transition only to independently reconstructed SDK bytes, never rebaseline guest mutations.
    image->raw_hash = entry_hash;
    host->ship_prolog_owner = image->owner;
    host->ship_prolog_stack = host->cpu.gpr[1];
    host->ship_prolog_r2 = host->cpu.gpr[2];
    host->ship_prolog_r13 = host->cpu.gpr[13];
    host->ship_prolog_ctor_completed = false;
    return NULL;
}
