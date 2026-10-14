#ifndef WW_REL_ICBI_PROBE_H
#define WW_REL_ICBI_PROBE_H

static void probe_rel_icbi(WwHost* host, const char* scenario) {
    CPUState* cpu = &host->cpu;
    assert(ww_host_materialize_rel(host, cpu->gpr[3], "d_a_ship") == NULL);
    assert(ww_host_link_rel(host, cpu->gpr[3]) == NULL);
    WwRelImage* image = ww_host_rel_live(host, 168u);
    assert(image != NULL && image->guest == WW_REL_SHIP_SCRATCH_BASE);
    const WwRelSection* section = &image->sections[1];
    const u32 start = image->guest + (section->flags & ~1u);
    const u32 end = start + section->size;
    const WwRelSection* data = &image->sections[host->rel_data[0].section_index];
    assert((data->flags & 1u) == 0u && data->flags != 0u && data->size >= 8u);
    const u32 data_target = image->guest + data->flags + 4u;
    assert(ww_host_rel_address(host, data_target) == data_target && host->error == NULL);
    if (image->section_count == 21u)
        assert(host->rel_data[0].section_index == 2u && data_target == 0x81F9DAF4u);
    u32 target = start;
    bool accepted = false;
    const char* refusal = "instruction cache invalidation requires unmodified mapped DOL code";
    if (strcmp(scenario, "icbi-start") == 0 || strcmp(scenario, "icbi-tagged") == 0)
        accepted = true;
    else if (strcmp(scenario, "icbi-interior") == 0) {
        target++;
        accepted = true;
    } else if (strcmp(scenario, "icbi-last") == 0) {
        target = end - 1u;
        accepted = true;
    } else if (strcmp(scenario, "icbi-image-start") == 0) {
        target = image->guest;
        accepted = true;
    } else if (strcmp(scenario, "icbi-image-last") == 0 || strcmp(scenario, "icbi-image-tagged") == 0) {
        target = image->guest + image->bytes - 1u;
        accepted = true;
    } else if (strncmp(scenario, "icbi-data", 9u) == 0) {
        target = data_target;
        accepted = true;
        if (strcmp(scenario, "icbi-data") != 0) {
            relocate_section_table(host, image);
            assert(ww_host_rel_sections_match_post_link(image) && !ww_host_rel_raw_unchanged(image));
            if (strcmp(scenario, "icbi-data-tampered") == 0) {
                mem_write32(cpu, image->guest + image->section_table + 12u, section->size + 4u);
                assert(!ww_host_rel_sections_match_post_link(image));
                accepted = false;
                refusal = "REL owned section table changed after validation";
            } else
                assert(strcmp(scenario, "icbi-data-relocated") == 0);
        }
    } else if (strcmp(scenario, "icbi-below") == 0)
        target = image->guest - 1u;
    else if (strcmp(scenario, "icbi-end") == 0)
        target = image->guest + image->bytes;
    else if (strcmp(scenario, "icbi-above") == 0)
        target = image->guest + image->bytes + 1u;
    else if (strcmp(scenario, "icbi-unknown") == 0)
        target = 0x90000000u;
    else if (strcmp(scenario, "icbi-unpublished") == 0) {
        target = data_target;
        mem_write32(cpu, image->owner + 0x10u, 0u);
    } else if (strcmp(scenario, "icbi-unlinked") == 0 || strcmp(scenario, "icbi-collected") == 0) {
        target = data_target;
        image->linked = false;
        image->unlinked = true;
        mem_write32(cpu, image->owner + 0x10u, 0u);
        if (strcmp(scenario, "icbi-collected") == 0)
            assert(ww_host_collect_rels(host) == NULL);
    } else if (strcmp(scenario, "icbi-no-contract") == 0)
        host->ship_contract = NULL;
    else
        assert(false);
    if (strcmp(scenario, "icbi-tagged") == 0 || strcmp(scenario, "icbi-image-tagged") == 0)
        target |= 0x40000000u;
    cpu->pc = 0x803031D8u;
    cpu->gpr[3] = target;
    cpu->instruction_fallback(cpu, 0x7C001FACu, cpu->pc);
    if (accepted) {
        if (host->error != NULL)
            fprintf(stderr, "STOP reason=%s address=%08X\n", host->error, host->error_address);
        assert(host->error == NULL && cpu->exception == 0u && cpu->pc == 0x803031DCu);
    } else {
        assert(host->error != NULL && cpu->exception == PPC_EXC_PROGRAM && cpu->pc == 0x803031D8u);
        assert(strcmp(host->error, refusal) == 0);
        assert(host->error_address == target && host->error_pc == cpu->pc);
    }
    fprintf(stderr, "PASS icbi %s target=%08X executable=[%08X,%08X)\n", accepted ? "accepted" : "refused", target, start, end);
}

#endif
