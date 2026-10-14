#ifndef WW_SHIP_PUBLICATION_PROBE_H
#define WW_SHIP_PUBLICATION_PROBE_H

static void relocate_ship_payload(WwHost* host, const WwRelImage* image) {
    CPUState* cpu = &host->cpu;
    const u8* original = host->ship_contract->rel_bytes;
    const u32 imports = read_be32(original + 0x28u);
    for (u32 at = imports; at < imports + read_be32(original + 0x2Cu); at += 8u) {
        const u32 module = read_be32(original + at);
        assert(module == 0u || module == 168u);
        u32 patch = 0u;
        for (u32 cursor = read_be32(original + at + 4u);; cursor += 8u) {
            assert(cursor + 8u <= image->bytes);
            const u8* entry = original + cursor;
            const u32 type = entry[2];
            if (type == 203u) break;
            if (type == 202u) {
                patch = image->guest + (image->sections[entry[3]].flags & ~1u);
                continue;
            }
            patch += ((u32)entry[0] << 8u) | entry[1];
            if (type == 201u) continue;
            const u32 section = module == 0u ? 0u :
                mem_read32(cpu, image->guest + image->section_table + entry[3] * 8u) & ~1u;
            const u32 value = section + read_be32(entry + 4u);
            if (type == 1u) mem_write32(cpu, patch, value);
            else if (type == 4u) mem_write16(cpu, patch, (u16)value);
            else if (type == 6u) mem_write16(cpu, patch, (u16)((value + 0x8000u) >> 16u));
            else {
                assert(type == 10u);
                mem_write32(cpu, patch, (mem_read32(cpu, patch) & ~0x03FFFFFCu) |
                    ((value - patch) & 0x03FFFFFCu));
            }
        }
        mem_write32(cpu, image->guest + at + 4u, image->guest + read_be32(original + at + 4u));
    }
    for (u32 at = 0u; at < 3u; ++at) {
        const u32 field = (u32[]){0x24u, 0x28u, 0x48u}[at];
        mem_write32(cpu, image->guest + field, image->guest + read_be32(original + field));
    }
    mem_write8(cpu, image->guest + 0x33u, (u8)bss_section_index(image));
    mem_write32(cpu, image->guest + 0x8u, 0x81822E80u);
    mem_write32(cpu, image->guest + 0x2Cu, 0u);
    mem_write32(cpu, image->owner + 0x14u, 0x807FF620u);
    mem_write32(cpu, 0x800030CCu, image->guest);
}

static void rebase_ship_lifecycle(WwHost* host, const WwRelImage* image) {
    CPUState* cpu = &host->cpu;
    const u32 table = mem_read32(cpu, image->guest + 0x10u);
    for (u32 at = 0u; at < 3u; ++at) {
        const u32 index = mem_read8(cpu, image->guest + 0x30u + at);
        const u32 field = image->guest + 0x34u + at * 4u;
        const u32 section = mem_read32(cpu, table + index * 8u) & ~1u;
        const u32 offset = mem_read32(cpu, field);
        mem_write32(cpu, field, offset + section);
        fprintf(stderr, "SDK lifecycle field=%02X published=%08X section=%08X target=%08X\n",
                0x34u + at * 4u, offset, section, mem_read32(cpu, field));
    }
}

static void probe_ship_publication(WwHost* host, const char* scenario) {
    CPUState* cpu = &host->cpu;
    const bool sdk_entry = strncmp(scenario, "publication-sdk-entry", 21u) == 0;
    if (sdk_entry || strcmp(scenario, "publication-entry-pc") == 0) {
        cpu->gpr[3] = 0x80AE0918u;
        mem_write32(cpu, 0x800030CCu, 0x81822E80u);
    }
    const u32 owner = cpu->gpr[3];
    assert(ww_host_materialize_rel(host, owner, "d_a_ship") == NULL);
    WwRelImage* image = ww_host_rel_owned(host, owner);
    assert(image != NULL && image->guest == WW_REL_SHIP_SCRATCH_BASE);
    const u64 materialized_hash = image->raw_hash;
    assert(ww_host_link_rel(host, owner) == NULL);
    assert(ww_host_rel_live(host, 168u) == image);
    if (sdk_entry || strcmp(scenario, "publication-entry-pc") == 0) {
        if (sdk_entry) {
            relocate_section_table(host, image);
            rebase_ship_lifecycle(host, image);
            if (strcmp(scenario, "publication-sdk-entry-partial") != 0)
                relocate_ship_payload(host, image);
        }
        cpu->pc = image->guest + (image->sections[1].flags & ~1u);
        cpu->lr = 0x80240EE8u;
        cpu->gpr[3] = image->guest;
        cpu->gpr[31] = owner;
        const u32 entry = ww_host_rel_address(host, cpu->pc);
        assert((entry & ~0x40000000u) == 0x81B100F4u);
        if (strcmp(scenario, "publication-sdk-entry-pc") == 0 || strcmp(scenario, "publication-entry-pc") == 0)
            cpu->pc += 4u;
        if (strcmp(scenario, "publication-sdk-entry-lr") == 0) cpu->lr += 4u;
        if (strcmp(scenario, "publication-sdk-entry-r31") == 0) cpu->gpr[31] += 4u;
        if (strcmp(scenario, "publication-sdk-entry-r3") == 0) cpu->gpr[3] += 4u;
        if (strncmp(scenario, "publication-sdk-entry-tampered-", 31u) == 0) {
            const char* field = scenario + 31u;
            u32 offset = 0x14u;
            if (strcmp(field, "lifecycle") == 0) offset = 0x38u;
            else if (strcmp(field, "code") == 0) offset = (image->sections[1].flags & ~1u) + 4u;
            else if (strcmp(field, "data") == 0) offset = image->sections[host->rel_data[0].section_index].flags & ~1u;
            else if (strcmp(field, "stream") == 0) offset = read_be32(host->ship_contract->rel_bytes + 0x24u) + 4u;
            else if (strcmp(field, "bss") == 0) offset = image->section_table + bss_section_index(image) * 8u;
            else if (strcmp(field, "import") == 0) offset = 0x2Cu;
            else assert(strcmp(field, "header") == 0);
            mem_write8(cpu, image->guest + offset, mem_read8(cpu, image->guest + offset) ^ 1u);
        }
        const u64 live_hash = ww_host_rel_raw_hash(get_ram_ptr(cpu, image->guest, image->bytes, NULL), image->bytes);
        const char* error = ww_host_ship_boundary(host, entry);
        const bool accepted = strcmp(scenario, "publication-sdk-entry") == 0;
        if (accepted) {
            if (error != NULL) fprintf(stderr, "STOP SDK entry reason=%s pristine=%016llX live=%016llX\n",
                error, (unsigned long long)materialized_hash, (unsigned long long)live_hash);
            assert(error == NULL && host->ship_prolog_owner == owner && !host->ship_prolog_return_ready);
            assert(ww_host_rel_raw_unchanged(image));
            cpu->pc = entry;
            assert(ww_host_ship_boundary(host, entry) == NULL);
        } else {
            assert(error != NULL && strcmp(error, "ship prolog entry requires its exact translated dynamic-link caller") == 0);
            assert(host->ship_prolog_owner == 0u && image->raw_hash == materialized_hash);
        }
        assert(host->error == NULL);
        assert(ww_host_rel_raw_hash(get_ram_ptr(cpu, image->guest, image->bytes, NULL), image->bytes) == live_hash);
        fprintf(stderr, "PASS SDK entry %s pc=81F900F4 lr=80240EE8 r3=81F90000 owner=80AE0918\n",
                accepted ? "accepted" : "refused");
        return;
    }
    if (strcmp(scenario, "publication-offsets") == 0 || strcmp(scenario, "publication-relink") == 0) {
        const u8* raw = get_ram_ptr(cpu, image->guest, image->bytes, NULL);
        assert(raw != NULL && memcmp(raw, host->ship_contract->rel_bytes, image->bytes) == 0);
        assert(image->raw_hash == materialized_hash && ww_host_rel_raw_unchanged(image));
        if (strcmp(scenario, "publication-relink") == 0) {
            mem_write32(cpu, host->rel_data[host->rel_data_count - 1u].linked_start, 7u);
            assert(ww_host_link_rel(host, owner) == NULL);
            assert(mem_read32(cpu, host->rel_data[host->rel_data_count - 1u].linked_start) == 7u);
            assert(memcmp(raw, host->ship_contract->rel_bytes, image->bytes) == 0);
            assert(image->raw_hash == materialized_hash && ww_host_rel_raw_unchanged(image));
        }
        fprintf(stderr, "PASS publication pristine offsets=00000000,0000002C,00000058\n");
        return;
    }
    if (strncmp(scenario, "publication-tampered-", 21u) == 0) {
        const u32 at = strcmp(scenario + 21u, "prolog") == 0 ? 0u :
            strcmp(scenario + 21u, "epilog") == 0 ? 1u : 2u;
        const u32 field = image->guest + 0x34u + at * 4u;
        mem_write32(cpu, field, mem_read32(cpu, field) + 4u);
        const char* error = ww_host_link_rel(host, owner);
        assert(error != NULL && strcmp(error, "ship contract full raw REL identity mismatch") == 0);
        assert(image->raw_hash == materialized_hash && !ww_host_rel_raw_unchanged(image));
        fprintf(stderr, "PASS publication tampered refused field=%02X\n", 0x34u + at * 4u);
        return;
    }
    relocate_section_table(host, image);
    rebase_ship_lifecycle(host, image);
    assert(ww_host_rel_sections_match_post_link(image));
    for (u32 at = 0u; at < 3u; ++at) {
        const u8* original = host->ship_contract->rel_bytes;
        const WwRelSection* section = &image->sections[original[0x30u + at]];
        const u32 target = mem_read32(cpu, image->guest + 0x34u + at * 4u);
        const u32 offset = read_be32(original + 0x34u + at * 4u);
        assert(target == image->guest + (section->flags & ~1u) + offset);
        assert((ww_host_rel_address(host, target) & ~0x40000000u) ==
               ((section->linked_start + offset) & ~0x40000000u));
    }
    assert(host->error == NULL && image->raw_hash == materialized_hash && !ww_host_rel_raw_unchanged(image));
    cpu->gpr[31] = owner;
    const char* error = NULL;
    if (strcmp(scenario, "publication-sdk-ctor") == 0) {
        cpu->lr = image->sections[1].linked_start + 24u;
        cpu->gpr[3] = host->rel_data[0].linked_start;
        error = ww_host_rel_constructor_boundary(host, 0x80241178u);
        assert(error != NULL && strcmp(error, "REL constructor raw image changed after validation") == 0);
        assert(host->rel_constructor_owner == 0u);
    } else {
        assert(strcmp(scenario, "publication-sdk-target") == 0);
    }
    fprintf(stderr, "PASS publication SDK prolog canonical=%08X gate=%s\n",
            ww_host_rel_address(host, mem_read32(cpu, image->guest + 0x34u)) & ~0x40000000u,
            error == NULL ? "not-entered" : error);
}

#endif
