#ifndef WW_SHIP_PUBLICATION_PROBE_H
#define WW_SHIP_PUBLICATION_PROBE_H

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
    const u32 owner = cpu->gpr[3];
    assert(ww_host_materialize_rel(host, owner, "d_a_ship") == NULL);
    WwRelImage* image = ww_host_rel_owned(host, owner);
    assert(image != NULL && image->guest == WW_REL_SHIP_SCRATCH_BASE);
    const u64 materialized_hash = image->raw_hash;
    assert(ww_host_link_rel(host, owner) == NULL);
    assert(ww_host_rel_live(host, 168u) == image);
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
    if (strcmp(scenario, "publication-sdk-entry") == 0) {
        cpu->lr = 0x80240EE8u;
        error = ww_host_ship_boundary(host, ww_host_rel_address(host, mem_read32(cpu, image->guest + 0x34u)));
        assert(error != NULL && strcmp(error, "ship prolog entry requires its exact translated dynamic-link caller") == 0);
        assert(host->ship_prolog_owner == 0u && !host->ship_prolog_return_ready);
    } else if (strcmp(scenario, "publication-sdk-ctor") == 0) {
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
