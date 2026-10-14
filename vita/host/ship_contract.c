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
