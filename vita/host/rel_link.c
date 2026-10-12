#include "boot_host.h"
#include "rel_archive.h"
#include "rel_profile_list.h"

const char* ww_host_link_profile_list(WwHost* host, u32 object) {
    if (host == NULL || host->module == NULL)
        return "REL link requires an owned host and compiled descriptor";
    CPUState* cpu = &host->cpu;
    const u32 global_offset = BLUEWAKE_PROFILE_LIST_GLOBAL - GC_RAM_BASE;
    if (cpu->ram == NULL || global_offset > cpu->ram_size || 4u > cpu->ram_size - global_offset)
        return "REL link requires mapped profile-list global storage";
    WwRelImage* slot = &host->rel_image;
    if (slot->owner != object || slot->module_id != 1u)
        return "REL link supports only the owned profile-list module 1 contract";
    if ((cpu->lr & 3u) != 0u || cpu->lr == 0u)
        return "REL link requires an aligned real guest return";
    const BlueWakeRelData* profile = bluewake_rel_profile_list(host->rel_data, host->rel_data_count, slot->module_id);
    if (profile == NULL || profile->bytes == NULL)
        return "REL link requires the prelinked profile-list data export";
    const u8* linked = get_ram_ptr(cpu, profile->linked_start, profile->size, NULL);
    if (linked == NULL || memcmp(linked, profile->bytes, profile->size) != 0)
        return "REL link requires byte-identical mapped prelinked profile-list data";
    u8* raw;
    if (!ppc_guest_alias_get_storage(slot->guest, slot->capacity, &raw) || slot->bytes < 0x4Cu ||
        bluewake_archive_be32(raw) != 1u || bluewake_archive_be32(raw + 0xCu) <= 4u ||
        bluewake_archive_be32(raw + 0x20u) != 0u)
        return "REL profile-list link requires its owned raw header without unsupported BSS";
    const u32 table = bluewake_archive_be32(raw + 0x10u);
    if ((u64)table + 5u * 8u > slot->bytes || bluewake_archive_be32(raw + table + 4u * 8u + 4u) != profile->size)
        return "REL profile-list raw data section differs from the prelinked export";
    bool section_matches = false;
    for (u32 module = 0u; module < host->module->num_rel_modules; ++module) {
        const StaticRecompRelModule* rel = &host->module->rel_modules[module];
        if (rel->module_id != slot->module_id)
            continue;
        for (u32 section = 0u; section < rel->num_sections; ++section) {
            const StaticRecompRelSection* data = &rel->sections[section];
            if (data->section_index == profile->section_index && data->linked_start == profile->linked_start &&
                data->size == profile->size)
                section_matches = true;
        }
    }
    if (!section_matches || get_ram_ptr(cpu, BLUEWAKE_PROFILE_LIST_GLOBAL, 4u, NULL) !=
        cpu->ram + global_offset ||
        ww_host_linked_alias_overlaps(BLUEWAKE_PROFILE_LIST_GLOBAL, 4u))
        return "REL profile-list link requires the matching section and real MEM1 global";
    if (mem_read32(cpu, object + 0x10u) != (slot->linked ? slot->guest : 0u) ||
        mem_read32(cpu, BLUEWAKE_PROFILE_LIST_GLOBAL) != (slot->linked ? profile->linked_start : 0u))
        return "REL profile-list link would overwrite another guest owner";
    if (!slot->linked && fprintf(stderr, "[rel-link] module=1 owner=%08X guest=%08X profile=%08X bytes=%u contract=static-composite-profile-list\n",
                                object, slot->guest, profile->linked_start, profile->size) < 0)
        return "REL profile-list link trace write failed";
    mem_write32(cpu, object + 0x10u, slot->guest);
    mem_write32(cpu, BLUEWAKE_PROFILE_LIST_GLOBAL, profile->linked_start);
    slot->linked = true;
    cpu->gpr[3] = 1u;
    cpu->pc = cpu->lr;
    return NULL;
}
