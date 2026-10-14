#include "boot_host.h"
#include "rel_archive.h"
#include <stdlib.h>

static bool identity_valid(const char* identity) {
    return identity != NULL && strnlen(identity, 65u) == 64u &&
        strspn(identity, "0123456789abcdef") == 64u;
}

const char* ww_rel_check_dol(const u8* bytes, u32 size, const char* path) {
    if (bytes == NULL || size == 0u || size > 0x800000u || path == NULL)
        return "REL certificate requires bounded original DOL bytes";
    FILE* stream = fopen(path, "rb");
    if (stream == NULL)
        return "REL certificate raw DOL open failed";
    u8 buffer[4096];
    const char* error = NULL;
    for (u32 at = 0u; at < size && error == NULL;) {
        const u32 length = size - at < sizeof(buffer) ? size - at : sizeof(buffer);
        if (fread(buffer, 1u, length, stream) != length || memcmp(buffer, bytes + at, length) != 0)
            error = "REL certificate full raw DOL identity mismatch";
        at += length;
    }
    if (error == NULL && (fgetc(stream) != EOF || ferror(stream)))
        error = "REL certificate full raw DOL length or read mismatch";
    if (fclose(stream) != 0 && error == NULL)
        error = "REL certificate raw DOL close failed";
    return error;
}

static const BlueWakeRelData* canonical_section(const BlueWakeRelInitContractV1* certificate, u32 index) {
    const BlueWakeRelData* found = NULL;
    for (u32 at = 0u; at < certificate->section_count; ++at) {
        const BlueWakeRelData* section = &certificate->canonical_sections[at];
        if (section->section_index == index) {
            if (found != NULL)
                return NULL;
            found = section;
        }
    }
    return found;
}

static bool executable_pc(WwHost* host, const StaticRecompRelModule* rel, const u8* raw, u32 pc) {
    if ((pc & 3u) != 0u || !ww_rel_translated_section(host, pc, 4u))
        return false;
    const u32 table = bluewake_archive_be32(raw + 0x10u);
    for (u32 at = 0u; at < rel->num_sections; ++at) {
        const StaticRecompRelSection* section = &rel->sections[at];
        if ((bluewake_archive_be32(raw + table + section->section_index * 8u) & 1u) != 0u &&
            pc >= section->linked_start && (u64)pc + 4u <= (u64)section->linked_start + section->size)
            return true;
    }
    return false;
}

const char* ww_rel_validate_certificate(WwHost* host, const BlueWakeRelInitContractV1* certificate) {
    if (host == NULL || host->module == NULL || certificate == NULL || certificate->module_id == 0u ||
        certificate->module_id == 1u || certificate->rel_bytes == NULL || certificate->rel_size < 0x4Cu ||
        certificate->rel_size > 0x100000u || certificate->canonical_sections == NULL ||
        certificate->section_count == 0u || certificate->section_count > 256u ||
        !identity_valid(certificate->build_identity) || certificate->lifecycle == NULL ||
        certificate->lifecycle->module_id != certificate->module_id ||
        host->module->rel_modules == NULL || host->module->num_rel_modules > WW_REL_MAX_IMAGES ||
        host->module->code_ranges == NULL || host->module->chunk_ranges == NULL ||
        host->module->chunk_hashes == NULL || (host->rel_data_count != 0u && host->rel_data == NULL))
        return "REL certificate schema is incomplete or invalid";
    const StaticRecompRelModule* rel = ww_host_rel_descriptor(host, certificate->module_id);
    u32 descriptors = 0u;
    for (u32 at = 0u; at < host->module->num_rel_modules; ++at)
        descriptors += host->module->rel_modules[at].module_id == certificate->module_id;
    const u8* raw = certificate->rel_bytes;
    const u32 bytes = certificate->rel_size;
    const u32 count = bluewake_archive_be32(raw + 0xCu);
    const u32 table = bluewake_archive_be32(raw + 0x10u);
    const u32 version = bluewake_archive_be32(raw + 0x1Cu);
    if (descriptors != 1u || rel == NULL || rel->sections == NULL || count == 0u || count > 256u ||
        rel->num_sections > count || rel->section_count != count || rel->section_info_offset != table ||
        rel->file_size != bytes || rel->version != bluewake_archive_be32(raw + 4u) ||
        bluewake_archive_be32(raw) != certificate->module_id || version != 3u ||
        (u64)table + (u64)count * 8u > bytes)
        return "REL certificate raw header disagrees with its unique compiled descriptor";
    for (u32 at = 0u; at < rel->num_sections; ++at) {
        const StaticRecompRelSection* section = &rel->sections[at];
        if (section->module_id != rel->module_id || section->section_index >= count)
            return "REL certificate compiled section identity is invalid";
        for (u32 other = 0u; other < at; ++other)
            if (rel->sections[other].section_index == section->section_index)
                return "REL certificate compiled section identity is ambiguous";
    }
    const u32 alignments[] = {bluewake_archive_be32(raw + 0x40u), bluewake_archive_be32(raw + 0x44u)};
    for (u32 at = 0u; at < 2u; ++at)
        if (alignments[at] > 0x100000u || (alignments[at] & (alignments[at] - 1u)) != 0u)
            return "REL certificate SDK alignment is unsupported";
    for (u32 at = 0u; at < host->rel_data_count; ++at) {
        const BlueWakeRelData* data = &host->rel_data[at];
        if (data->module_id != rel->module_id)
            continue;
        if (data->section_index >= count || data->size == 0u ||
            (bluewake_archive_be32(raw + table + data->section_index * 8u) & 1u) != 0u ||
            bluewake_archive_be32(raw + table + data->section_index * 8u + 4u) != data->size)
            return "REL certificate rejects fabricated empty, executable or BSS exports";
    }
    const u32 entries[] = {certificate->lifecycle->prolog_address, certificate->lifecycle->epilog_address,
                           certificate->lifecycle->unresolved_address};
    for (u32 at = 0u; at < 3u; ++at) {
        const u32 index = raw[0x30u + at];
        const u32 offset = bluewake_archive_be32(raw + 0x34u + at * 4u);
        const StaticRecompRelSection* section = ww_rel_section(rel, index);
        if (index == 0u) {
            if (offset != 0u || entries[at] != 0u)
                return "REL certificate absent lifecycle selector has a nonzero entry";
        } else if (index >= count || section == NULL || (u64)offset + 4u > section->size ||
                   (u64)section->linked_start + offset != entries[at]) {
            return "REL certificate requires the exact approved lifecycle addresses";
        } else if (!executable_pc(host, rel, raw, entries[at])) {
            return "REL lifecycle entries require owned translated executable sections";
        }
    }
    u8* replay = malloc(bytes);
    if (replay == NULL)
        return "REL certificate replay allocation failed";
    memcpy(replay, raw, bytes);
    const char* error = NULL;
    u32 canonical_count = 0u, bss_index = 0u;
    u64 bss = 0u;
    for (u32 index = 0u; index < count && error == NULL; ++index) {
        const u32 flags = bluewake_archive_be32(raw + table + index * 8u);
        const u32 size = bluewake_archive_be32(raw + table + index * 8u + 4u);
        const u32 offset = flags & ~1u;
        const StaticRecompRelSection* section = ww_rel_section(rel, index);
        if (size == 0u) {
            if (flags != 0u || (section != NULL && (section->size != 0u || section->linked_start != 0u)))
                error = "REL certificate empty section flags or size are invalid";
            continue;
        }
        if (index == 0u || section == NULL || section->size != size || section->linked_start == 0u ||
            (u64)section->linked_start + size > UINT32_MAX + 1ull ||
            (offset != 0u && (u64)offset + size > bytes)) {
            error = "REL certificate section disagrees with its compiled descriptor";
        } else if (offset == 0u) {
            if (flags != 0u || bss_index != 0u)
                error = "REL certificate BSS selector is ambiguous or executable";
            bss += size;
            bss_index = index;
            const BlueWakeRelData* data = ww_rel_data(host, rel->module_id, index);
            if (data == NULL || data->linked_start != section->linked_start || data->size != size || data->bytes != NULL)
                error = "REL certificate BSS has no unique compiled export";
        } else {
            canonical_count++;
            const BlueWakeRelData* canonical = canonical_section(certificate, index);
            if (canonical == NULL || canonical->module_id != rel->module_id ||
                canonical->linked_start != section->linked_start || canonical->size != size || canonical->bytes == NULL)
                error = "REL certificate canonical section identity is missing or ambiguous";
        }
    }
    if (error == NULL && (canonical_count != certificate->section_count ||
        bss != bluewake_archive_be32(raw + 0x20u) || (bss == 0u && raw[0x33u] != 0u)))
        error = "REL certificate canonical count or BSS metadata differs from its raw image";
    if (error == NULL)
        error = ww_rel_replay(host, rel, raw, bytes, replay, NULL, true);
    for (u32 index = 0u; index < count && error == NULL; ++index) {
        const u32 flags = bluewake_archive_be32(raw + table + index * 8u);
        const u32 size = bluewake_archive_be32(raw + table + index * 8u + 4u);
        const u32 offset = flags & ~1u;
        if (size == 0u || offset == 0u)
            continue;
        const BlueWakeRelData* canonical = canonical_section(certificate, index);
        if (memcmp(replay + offset, canonical->bytes, size) != 0)
            error = "REL raw relocation semantics disagree with the canonical producer image";
        else if ((flags & 1u) != 0u && (!ww_rel_translated_section(host, canonical->linked_start, size) ||
                 !ww_rel_code_matches(host, ww_rel_section(rel, index), replay, offset)))
            error = "REL canonical code disagrees with compiled chunk hashes";
        else if ((flags & 1u) == 0u) {
            const BlueWakeRelData* data = ww_rel_data(host, rel->module_id, index);
            if (data == NULL || data->linked_start != canonical->linked_start || data->size != size ||
                data->bytes == NULL || memcmp(data->bytes, canonical->bytes, size) != 0)
                error = "REL canonical data disagrees with its unique compiled export";
        }
    }
    const BlueWakeRelData* ctors = canonical_section(certificate, certificate->ctor_table_section);
    if (error == NULL && (ctors == NULL ||
        (bluewake_archive_be32(raw + table + certificate->ctor_table_section * 8u) & 1u) != 0u ||
        (certificate->ctor_table_offset & 3u) != 0u ||
        (u64)certificate->ctor_table_offset + 4u > ctors->size ||
        !executable_pc(host, rel, raw, certificate->ctor_return_pc)))
        error = "REL certificate constructor table or successor is invalid";
    bool terminated = false;
    for (u32 at = certificate->ctor_table_offset; error == NULL && (u64)at + 4u <= ctors->size; at += 4u) {
        const u32 pc = bluewake_archive_be32(ctors->bytes + at);
        if (pc == 0u) {
            terminated = true;
            break;
        }
        if (!executable_pc(host, rel, raw, pc))
            error = "REL certificate constructor target is not owned executable code";
    }
    if (error == NULL && !terminated)
        error = "REL certificate constructor table has no bounded terminator";
    free(replay);
    return error;
}

const char* ww_host_register_rel_contracts(WwHost* host, const BlueWakeRelInitContractSetV1* set, const char* dol) {
    if (host == NULL || host->rel_contracts != NULL || set == NULL || set->version != 1u ||
        set->contracts == NULL || set->contract_count == 0u || set->contract_count > WW_REL_MAX_IMAGES ||
        !identity_valid(set->build_identity))
        return "REL certificate set schema, count or identity is invalid";
    const char* error = ww_rel_check_dol(set->dol_bytes, set->dol_size, dol);
    for (u32 at = 0u; error == NULL && at < set->contract_count; ++at) {
        for (u32 other = 0u; other < at; ++other)
            if (set->contracts[at].module_id == set->contracts[other].module_id)
                error = "REL certificate set module identity is ambiguous";
        if (error == NULL)
            error = ww_rel_validate_certificate(host, &set->contracts[at]);
    }
    if (error == NULL)
        host->rel_contracts = set;
    return error;
}

const BlueWakeRelInitContractV1* ww_host_rel_certificate(WwHost* host, u32 module_id) {
    if (host->ship_contract != NULL && module_id == host->ship_certificate.module_id)
        return &host->ship_certificate;
#if WW05_TESTS_ONLY
    if (host->rel_test_admission && host->rel_contracts != NULL)
        for (u32 at = 0u; at < host->rel_contracts->contract_count; ++at)
            if (host->rel_contracts->contracts[at].module_id == module_id)
                return &host->rel_contracts->contracts[at];
#endif
    return NULL;
}

#if WW05_TESTS_ONLY
const char* ww_host_rel_test_admission(WwHost* host) {
    if (host == NULL || host->rel_contracts == NULL || host->rel_image_count != 0u)
        return "REL test admission requires a registered set before materialization";
    host->rel_test_admission = true;
    return NULL;
}
#endif
