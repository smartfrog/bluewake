#include "boot_host.h"
#include "rel_archive.h"
#include "rel_profile_list.h"
#include <stdlib.h>

static const StaticRecompRelSection* rel_section(const StaticRecompRelModule* rel, u32 index) {
    if (rel == NULL || rel->sections == NULL)
        return NULL;
    for (u32 at = 0u; at < rel->num_sections; ++at)
        if (rel->sections[at].section_index == index)
            return &rel->sections[at];
    return NULL;
}

static const BlueWakeRelData* rel_data(WwHost* host, u32 module, u32 section) {
    const BlueWakeRelData* found = NULL;
    for (u32 at = 0u; at < host->rel_data_count; ++at) {
        const BlueWakeRelData* data = &host->rel_data[at];
        if (data->module_id != module || data->section_index != section)
            continue;
        if (found != NULL)
            return NULL;
        found = data;
    }
    return found;
}

static bool translated_section(WwHost* host, u32 start, u32 size) {
    u64 cursor = start;
    const u64 end = cursor + size;
    for (u32 at = 0u; at < host->module->num_code_ranges && cursor < end; ++at) {
        const StaticRecompRange* range = &host->module->code_ranges[at];
        if (range->start <= cursor && cursor < range->end)
            cursor = range->end;
    }
    return cursor >= end;
}

static bool raw_code_matches(WwHost* host, const StaticRecompRelSection* section, const u8* raw, u32 offset) {
    const StaticRecompModuleDesc* module = host->module;
    if (module->chunk_ranges == NULL || module->chunk_hashes == NULL)
        return false;
    u64 cursor = section->linked_start;
    const u64 end = cursor + section->size;
    for (u32 at = 0u; at < module->num_chunk_ranges && cursor < end; ++at) {
        const StaticRecompRange* chunk = &module->chunk_ranges[at];
        if (chunk->start < cursor)
            continue;
        if (chunk->start != cursor || chunk->end <= chunk->start || chunk->end > end)
            return false;
        u64 hash = 14695981039346656037ull;
        for (u32 byte = chunk->start; byte < chunk->end; ++byte) {
            hash ^= raw[offset + byte - section->linked_start];
            hash *= 1099511628211ull;
        }
        if (hash != module->chunk_hashes[at])
            return false;
        cursor = chunk->end;
    }
    return cursor == end;
}

static void write_be32(u8* bytes, u32 value) {
    bytes[0] = (u8)(value >> 24u);
    bytes[1] = (u8)(value >> 16u);
    bytes[2] = (u8)(value >> 8u);
    bytes[3] = (u8)value;
}

static const char* validate_relocations(WwHost* host, const StaticRecompRelModule* rel, const u8* raw,
                                       u32 bytes, u8* relocated) {
    const u32 imports = bluewake_archive_be32(raw + 0x28u);
    const u32 import_bytes = bluewake_archive_be32(raw + 0x2Cu);
    if ((u64)imports + import_bytes > bytes || (import_bytes & 7u) != 0u)
        return "REL relocation import table is invalid";
    if (rel->module_id != 1u && import_bytes / 8u > WW_REL_MAX_IMPORTS)
        return "REL import count exceeds the bounded ownership contract";
    for (u32 at = imports; at < imports + import_bytes; at += 8u) {
        const u32 target_id = bluewake_archive_be32(raw + at);
        const StaticRecompRelModule* target = target_id == 0u ? NULL : ww_host_rel_descriptor(host, target_id);
        if (target_id != 0u && target == NULL)
            return "REL relocation imports an untranslated module";
        if (rel->module_id != 1u && target_id != 0u && target_id != rel->module_id &&
            ww_host_rel_live(host, target_id) == NULL)
            return "REL actor import requires a live dependency owner";
        u32 cursor = bluewake_archive_be32(raw + at + 4u);
        const StaticRecompRelSection* patch = NULL;
        u64 offset = 0u;
        bool ended = false;
        while ((u64)cursor + 8u <= bytes) {
            const u8* entry = raw + cursor;
            const u32 type = entry[2], section = entry[3];
            const u32 symbol = bluewake_archive_be32(entry + 4u);
            cursor += 8u;
            if (type == 203u) {
                ended = true;
                break;
            }
            if (type == 202u) {
                patch = rel_section(rel, section);
                offset = 0u;
                if (patch == NULL || patch->size == 0u)
                    return "REL relocation selects an absent patch section";
                continue;
            }
            offset += ((u32)entry[0] << 8u) | entry[1];
            if (patch == NULL || offset > patch->size)
                return "REL relocation patch offset exceeds its section";
            if (type == 201u)
                continue;
            const u32 width = type >= 3u && type <= 6u ? 2u : 4u;
            if (!(type == 1u || (type >= 3u && type <= 6u) || type == 10u || type == 11u) ||
                offset + width > patch->size || (offset & (width - 1u)) != 0u)
                return "REL relocation type, width or alignment is unsupported";
            const u32 table = bluewake_archive_be32(raw + 0x10u);
            if ((u64)table + (u64)patch->section_index * 8u + 8u > bytes)
                return "REL relocation patch table is outside its raw image";
            const u32 patch_flags = bluewake_archive_be32(raw + table + patch->section_index * 8u);
            const u32 patch_start = patch_flags & ~1u;
            if (patch_start == 0u)
                return "REL relocations into BSS are unsupported";
            if ((u64)patch_start + patch->size > bytes)
                return "REL relocation patch storage exceeds its raw image";
            u32 address = symbol;
            const StaticRecompRelSection* destination = NULL;
            if (target != NULL) {
                destination = rel_section(target, section);
                if (destination == NULL || destination->linked_start == 0u || symbol >= destination->size ||
                    (u64)destination->linked_start + symbol > UINT32_MAX)
                    return "REL relocation target exceeds its compiled section";
                address = destination->linked_start + symbol;
            } else if (symbol < GC_RAM_BASE || (u64)symbol >= (u64)GC_RAM_BASE + host->cpu.ram_size) {
                return "REL relocation DOL target is outside MEM1";
            }
            u32 value = address;
            if (type == 10u || type == 11u) {
                const u32 raw_word = bluewake_archive_be32(raw + patch_start + (u32)offset);
                if ((address & 3u) != 0u || !translated_section(host, address, 4u) ||
                    (destination != NULL && (rel_data(host, target_id, section) != NULL ||
                                             (u64)symbol + 4u > destination->size)))
                    return "REL branch target requires aligned translated executable coverage";
                const s64 displacement = (s64)address - ((s64)patch->linked_start + (s64)offset);
                const s64 minimum = type == 10u ? -0x02000000ll : -0x8000ll;
                const s64 maximum = type == 10u ? 0x01FFFFFCll : 0x7FFCLL;
                if (displacement < minimum || displacement > maximum || (displacement & 3ll) != 0ll ||
                    (raw_word >> 26u) != (type == 10u ? 18u : 16u) || (raw_word & 2u) != 0u)
                    return "REL branch displacement or instruction is not representable";
                const u32 mask = type == 10u ? 0x03FFFFFCu : 0x0000FFFCu;
                value = (raw_word & ~mask) | ((u32)displacement & mask);
            } else if (type == 5u) {
                value >>= 16u;
            } else if (type == 6u) {
                value = (value + 0x8000u) >> 16u;
            }
            if (relocated != NULL && (patch_flags & 1u) == 0u) {
                u8* write = relocated + patch_start + (u32)offset;
                if (width == 2u) {
                    write[0] = (u8)(value >> 8u);
                    write[1] = (u8)value;
                } else {
                    write_be32(write, value);
                }
            }
            if (rel->module_id != 1u && (patch_flags & 1u) != 0u)
                return "REL executable relocation lacks a raw-to-translated certification contract";
        }
        if (!ended)
            return "REL relocation stream has no bounded end";
    }
    return NULL;
}

const char* ww_host_validate_rel_link(WwHost* host, const StaticRecompRelModule* rel, const u8* raw, u32 bytes) {
    if (bytes < 0x4Cu || bytes > 0x100000u)
        return "REL prelinked validation requires a bounded raw header";
    if (rel->module_id != 1u && (raw[0x30u] != 0u || raw[0x31u] != 0u || raw[0x32u] != 0u ||
        bluewake_archive_be32(raw + 0x34u) != 0u ||
        bluewake_archive_be32(raw + 0x38u) != 0u || bluewake_archive_be32(raw + 0x3Cu) != 0u))
        return "REL prolog, epilog or unresolved entry requires an unsupported lifecycle contract";
    if (host->rel_data_count != 0u && host->rel_data == NULL)
        return "REL link requires a compiled data export";
    if (rel->sections == NULL || rel->num_sections > rel->section_count || rel->section_count > 256u)
        return "REL compiled section catalog is invalid";
    for (u32 at = 0u; at < rel->num_sections; ++at) {
        const StaticRecompRelSection* section = &rel->sections[at];
        if (section->section_index >= rel->section_count || section->module_id != rel->module_id)
            return "REL compiled section identity is invalid";
        for (u32 other = 0u; other < at; ++other)
            if (rel->sections[other].section_index == section->section_index)
                return "REL compiled section identity is ambiguous";
    }
    const u32 count = bluewake_archive_be32(raw + 0xCu);
    const u32 table = bluewake_archive_be32(raw + 0x10u);
    if (count != rel->section_count || table != rel->section_info_offset || (u64)table + count * 8u > bytes)
        return "REL raw section table changed before linking";
    u64 bss = 0u;
    for (u32 index = 0u; index < count; ++index) {
        const u32 flags = bluewake_archive_be32(raw + table + index * 8u);
        const u32 size = bluewake_archive_be32(raw + table + index * 8u + 4u);
        const u32 offset = flags & ~1u;
        const StaticRecompRelSection* section = rel_section(rel, index);
        if (size == 0u) {
            if (section != NULL && section->size != 0u)
                return "REL empty raw section disagrees with its compiled section";
            continue;
        }
        if (section == NULL || section->module_id != rel->module_id || section->size != size ||
            section->linked_start == 0u || (u64)section->linked_start + size > UINT32_MAX + 1ull ||
            (offset != 0u && (u64)offset + size > bytes))
            return "REL raw section differs from its compiled section";
        for (u32 at = 0u; at < WW_REL_MAX_IMAGES; ++at) {
            const WwRelImage* owner = &host->rel_images[at];
            if (!owner->linked || owner->module_id == rel->module_id)
                continue;
            const StaticRecompRelModule* owned = ww_host_rel_descriptor(host, owner->module_id);
            if (owned == NULL || owned->sections == NULL)
                return "REL live owner has no compiled section catalog";
            for (u32 other = 0u; other < owned->num_sections; ++other) {
                const StaticRecompRelSection* range = &owned->sections[other];
                const u32 start = section->linked_start & ~0x40000000u;
                const u32 occupied = range->linked_start & ~0x40000000u;
                if (range->size != 0u && (u64)start < (u64)occupied + range->size &&
                    (u64)occupied < (u64)start + size)
                    return "REL linked section overlaps another live module owner";
            }
        }
        if ((flags & 1u) != 0u) {
            if (offset == 0u || !translated_section(host, section->linked_start, size))
                return "REL executable section has no complete translated code range";
            if (!raw_code_matches(host, section, raw, offset))
                return "REL raw executable bytes disagree with the compiled chunk hashes";
            continue;
        }
        const BlueWakeRelData* data = rel_data(host, rel->module_id, index);
        if (data == NULL || data->linked_start != section->linked_start || data->size != size ||
            (offset == 0u) != (data->bytes == NULL) ||
            get_ram_ptr(&host->cpu, data->linked_start, size, NULL) == NULL)
            return "REL data or BSS has no matching mapped prelinked export";
        if (offset == 0u)
            bss += size;
    }
    if (bss != bluewake_archive_be32(raw + 0x20u))
        return "REL BSS size differs from its compiled exports";
    u8* relocated = malloc(bytes);
    if (relocated == NULL)
        return "REL prelinked verification allocation failed";
    memcpy(relocated, raw, bytes);
    const char* error = validate_relocations(host, rel, raw, bytes, relocated);
    for (u32 index = 0u; error == NULL && index < count; ++index) {
        const u32 flags = bluewake_archive_be32(raw + table + index * 8u);
        const u32 size = bluewake_archive_be32(raw + table + index * 8u + 4u);
        const u32 offset = flags & ~1u;
        if (size == 0u || offset == 0u || (flags & 1u) != 0u)
            continue;
        const BlueWakeRelData* data = rel_data(host, rel->module_id, index);
        if (memcmp(relocated + offset, data->bytes, size) != 0)
            error = "REL relocated raw data disagrees with the compiled prelinked export";
    }
    free(relocated);
    return error;
}

const char* ww_host_link_rel(WwHost* host, u32 object) {
    if (host == NULL || host->module == NULL || host->cpu.ram == NULL)
        return "REL link requires an owned host and compiled descriptor";
    WwRelImage* slot = ww_host_rel_owned(host, object);
    if (slot == NULL)
        return "REL link requires an owned materialized image";
    if (!ww_host_rel_sections_match(slot))
        return "REL owned section table changed after validation";
    if (slot->module_id == 1u)
        return ww_host_link_profile_list(host, object);
    CPUState* cpu = &host->cpu;
    if (cpu->lr == 0u || (cpu->lr & 3u) != 0u ||
        mem_read32(cpu, object + 0x10u) != (slot->linked ? slot->guest : 0u))
        return "REL link requires a real return and unchanged guest owner";
    u8* raw = NULL;
    const StaticRecompRelModule* rel = ww_host_rel_descriptor(host, slot->module_id);
    if (rel == NULL || !ppc_guest_alias_get_storage(slot->guest, slot->capacity, &raw) ||
        bluewake_archive_be32(raw) != slot->module_id || slot->bytes != rel->file_size)
        return "REL link requires the matching owned raw image";
    if (slot->unlinked)
        return "REL relink requires destruction of its explicitly unlinked scratch first";
    if (slot->linked) {
        const u32 imports = bluewake_archive_be32(raw + 0x28u);
        const u32 import_bytes = bluewake_archive_be32(raw + 0x2Cu);
        if ((u64)imports + import_bytes > slot->bytes || (import_bytes & 7u) != 0u)
            return "REL live dependency table changed";
        u32 count = 0u;
        for (u32 at = imports; at < imports + import_bytes; at += 8u) {
            const u32 dependency = bluewake_archive_be32(raw + at);
            if (dependency == 0u || dependency == 1u || dependency == slot->module_id)
                continue;
            if (count >= slot->dependency_count || dependency != slot->dependencies[count++])
                return "REL live dependency ownership changed";
        }
        if (count != slot->dependency_count)
            return "REL live dependency ownership changed";
    }
    const char* error = ww_host_validate_rel_link(host, rel, raw, slot->bytes);
    if (error != NULL)
        return error;
    if (!slot->linked) {
        if (fprintf(stderr, "[rel-link] module=%u owner=%08X guest=%08X contract=static-composite\n",
                    slot->module_id, object, slot->guest) < 0)
            return "REL link trace write failed";
        for (u32 at = 0u; at < rel->num_sections; ++at) {
            const StaticRecompRelSection* section = &rel->sections[at];
            const u32 flags = bluewake_archive_be32(raw + rel->section_info_offset + section->section_index * 8u);
            if (section->size == 0u || (flags & 1u) != 0u)
                continue;
            const BlueWakeRelData* data = rel_data(host, slot->module_id, section->section_index);
            if (data == NULL)
                continue;
            u8* storage = get_ram_ptr(cpu, data->linked_start, data->size, NULL);
            if (data->bytes == NULL)
                memset(storage, 0, data->size);
            else
                memcpy(storage, data->bytes, data->size);
        }
        mem_write32(cpu, object + 0x10u, slot->guest);
        const u32 imports = bluewake_archive_be32(raw + 0x28u);
        const u32 import_bytes = bluewake_archive_be32(raw + 0x2Cu);
        for (u32 at = imports; at < imports + import_bytes; at += 8u) {
            const u32 dependency = bluewake_archive_be32(raw + at);
            if (dependency != 0u && dependency != 1u && dependency != slot->module_id)
                slot->dependencies[slot->dependency_count++] = dependency;
        }
        slot->linked = true;
    }
    cpu->gpr[3] = 1u;
    cpu->pc = cpu->lr;
    return NULL;
}

bool ww_host_rel_sections_match(const WwRelImage* slot) {
    u8* raw = NULL;
    if (slot->sections == NULL || !ppc_guest_alias_get_storage(slot->guest, slot->capacity, &raw) ||
        bluewake_archive_be32(raw + 0xCu) != slot->section_count ||
        bluewake_archive_be32(raw + 0x10u) != slot->section_table)
        return false;
    for (u32 at = 0u; at < slot->section_count; ++at)
        if (bluewake_archive_be32(raw + slot->section_table + at * 8u) != slot->sections[at].flags ||
            bluewake_archive_be32(raw + slot->section_table + at * 8u + 4u) != slot->sections[at].size)
            return false;
    return true;
}

u32 ww_host_rel_address(WwHost* host, u32 address) {
    const u32 canonical = address & ~0x40000000u;
    if (canonical < WW_REL_SCRATCH_BASE || canonical >= WW_REL_SCRATCH_LIMIT)
        return address;
    for (u32 at = 0u; at < host->rel_image_count; ++at) {
        const WwRelImage* slot = &host->rel_images[at];
        if (canonical < slot->guest || (u64)canonical >= (u64)slot->guest + slot->bytes)
            continue;
        if (!slot->linked || mem_read32(&host->cpu, slot->owner + 0x10u) != slot->guest)
            continue;
        if (!ww_host_rel_sections_match(slot)) {
            ww_host_fail(host, "REL owned section table changed after validation", address, host->cpu.pc);
            return address;
        }
        for (u32 section = 0u; section < slot->section_count; ++section) {
            const WwRelSection* linked = &slot->sections[section];
            const u32 start = slot->guest + (linked->flags & ~1u);
            if (linked->linked_start != 0u && (linked->flags & 1u) != 0u &&
                canonical >= start && (u64)canonical < (u64)start + linked->size)
                return linked->linked_start + canonical - start;
        }
    }
    return address;
}

const char* ww_host_link_profile_list(WwHost* host, u32 object) {
    if (host == NULL || host->module == NULL)
        return "REL link requires an owned host and compiled descriptor";
    CPUState* cpu = &host->cpu;
    const u32 global_offset = BLUEWAKE_PROFILE_LIST_GLOBAL - GC_RAM_BASE;
    if (cpu->ram == NULL || global_offset > cpu->ram_size || 4u > cpu->ram_size - global_offset)
        return "REL link requires mapped profile-list global storage";
    WwRelImage* slot = ww_host_rel_owned(host, object);
    if (slot == NULL || slot->module_id != 1u)
        return "REL link supports only the owned profile-list module 1 contract";
    if (!ww_host_rel_sections_match(slot))
        return "REL owned section table changed after validation";
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
    const StaticRecompRelModule* descriptor = ww_host_rel_descriptor(host, slot->module_id);
    if (descriptor == NULL)
        return "REL profile-list descriptor is absent";
    const char* relocation_error = validate_relocations(host, descriptor, raw, slot->bytes, NULL);
    if (relocation_error != NULL)
        return relocation_error;
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
