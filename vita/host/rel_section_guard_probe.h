#ifndef WW_REL_SECTION_GUARD_PROBE_H
#define WW_REL_SECTION_GUARD_PROBE_H

static u32 bss_section_index(const WwRelImage* image) {
    for (u32 at = 0u; at < image->section_count; ++at)
        if (image->sections[at].flags == 0u && image->sections[at].size != 0u)
            return at;
    assert(false);
    return 0u;
}

static void relocate_section_table(WwHost* host, const WwRelImage* image) {
    CPUState* cpu = &host->cpu;
    mem_write32(cpu, image->guest + 0x10u, image->guest + image->section_table);
    for (u32 at = 0u; at < image->section_count; ++at) {
        const u32 flags = image->sections[at].flags;
        const u32 offset = flags & ~1u;
        const u32 value = offset != 0u ? (image->guest + offset) | (flags & 1u) : 0u;
        mem_write32(cpu, image->guest + image->section_table + at * 8u, value);
    }
    const u32 bss = bss_section_index(image);
    mem_write32(cpu, image->guest + image->section_table + bss * 8u, 0x807FF620u);
    if (image->section_count == 21u) {
        const u32 ww80[] = {0u, 0x81F900F5u, 0x81F9DAF0u, 0x81F9DAF8u,
                           0x81F9DB00u, 0x81F9DEE0u, 0x807FF620u};
        assert(mem_read32(cpu, image->guest + 0x10u) == 0x81F9004Cu);
        for (u32 at = 0u; at < 7u; ++at)
            assert(mem_read32(cpu, image->guest + image->section_table + at * 8u) == ww80[at]);
    }
}

static void probe_rel_sections(WwHost* host, const char* site, const char* form) {
    CPUState* cpu = &host->cpu;
    const u32 owner = cpu->gpr[3];
    assert(ww_host_materialize_rel(host, owner, "d_a_ship") == NULL);
    WwRelImage* image = ww_host_rel_owned(host, owner);
    assert(image != NULL && image->guest == 0x81F90000u);
    assert(ww_host_rel_sections_match(image));
    assert(ww_host_link_rel(host, owner) == NULL);
    const u32 target = image->guest + (image->sections[1].flags & ~1u);
    const u32 linked = image->sections[1].linked_start;
    const u32 table = image->guest + image->section_table;
    const u32 bss = table + bss_section_index(image) * 8u;
    const bool constructor = strncmp(site, "ctor", 4u) == 0;
    const BlueWakeRelData* ctors = &host->rel_data[0];
    assert(ctors->bytes != NULL);
    cpu->lr = linked + 24u;
    cpu->gpr[3] = ctors->linked_start;
    if (strcmp(site, "ctor-resume") == 0)
        assert(ww_host_rel_constructor_boundary(host, 0x80241178u) == NULL);
    const u64 published_hash = image->raw_hash;
    const bool pristine = strcmp(form, "pristine") == 0;
    if (!pristine)
        relocate_section_table(host, image);
    bool valid = pristine || strcmp(form, "relocated") == 0 || strcmp(form, "raw-hash") == 0;
    if (strncmp(form, "bss-", 4u) == 0) {
        u32 value = 0u;
        if (strcmp(form, "bss-zero") == 0) valid = true;
        else if (strcmp(form, "bss-low") == 0) { value = 0x80000000u; valid = true; }
        else if (strcmp(form, "bss-last") == 0) { value = 0x817FFFFCu; valid = true; }
        else if (strcmp(form, "bss-below") == 0) value = 0x7FFFFFFCu;
        else if (strcmp(form, "bss-end") == 0) value = 0x81800000u;
        else if (strcmp(form, "bss-ship") == 0) value = 0x81F9E3C0u;
        else if (strcmp(form, "bss-tagged") == 0) value = 0xC07FF620u;
        else { assert(strcmp(form, "bss-misaligned") == 0); value = 0x807FF622u; }
        mem_write32(cpu, bss, value);
    } else if (strcmp(form, "size") == 0)
        mem_write32(cpu, table + 12u, image->sections[1].size + 4u);
    else if (strcmp(form, "count") == 0)
        mem_write32(cpu, image->guest + 0xCu, image->section_count + 1u);
    else if (strcmp(form, "offset") == 0)
        mem_write32(cpu, table + 8u, mem_read32(cpu, table + 8u) + 4u);
    else if (strcmp(form, "exec") == 0)
        mem_write32(cpu, table + 8u, mem_read32(cpu, table + 8u) ^ 1u);
    else if (strcmp(form, "table") == 0)
        mem_write32(cpu, image->guest + 0x10u, image->guest + image->section_table + 4u);
    else if (strcmp(form, "mixed-pristine-table") == 0)
        mem_write32(cpu, image->guest + 0x10u, image->section_table);
    else if (strcmp(form, "mixed-relocated-table") == 0)
        mem_write32(cpu, table + 8u, image->sections[1].flags);
    else
        assert(valid);
    assert(image->raw_hash == published_hash);
    if (constructor && !pristine) {
        assert(!ww_host_rel_raw_unchanged(image));
        if (strcmp(form, "raw-hash") != 0) {
            // Isolate the section guard; the separate whole-image hash is unchanged in production.
            const u8* raw = get_ram_ptr(cpu, image->guest, image->bytes, NULL);
            assert(raw != NULL);
            image->raw_hash = ww_host_rel_raw_hash(raw, image->bytes);
        }
    }
    cpu->pc = 0x803031D8u;
    const WwRelImage image_before = *image;
    const u8* raw_before = get_ram_ptr(cpu, image->guest, image->bytes, NULL);
    assert(raw_before != NULL);
    const u64 raw_hash_before = ww_host_rel_raw_hash(raw_before, image->bytes);
    const u64 sections_hash_before = ww_host_rel_raw_hash((const u8*)image->sections,
        image->section_count * sizeof(*image->sections));
    const char* error = NULL;
    if (strcmp(site, "link") == 0) {
        valid = pristine;
        error = ww_host_link_rel(host, owner);
    } else if (strcmp(site, "resolver") == 0) {
        assert(ww_host_rel_address(host, target) == (valid ? linked : target));
        error = host->error;
    } else if (strcmp(site, "icbi") == 0) {
        cpu->gpr[3] = target;
        cpu->instruction_fallback(cpu, 0x7C001FACu, cpu->pc);
        assert(cpu->pc == (valid ? 0x803031DCu : 0x803031D8u));
        assert(cpu->exception == (valid ? 0u : PPC_EXC_PROGRAM));
        error = host->error;
    } else if (strcmp(site, "ownership") == 0) {
        assert(ww_host_rel_code_owned(host, linked) == valid);
        error = host->error;
    } else {
        assert(constructor);
        if (strcmp(form, "raw-hash") == 0) valid = false;
        error = ww_host_rel_constructor_boundary(host,
            strcmp(site, "ctor-resume") == 0 ? linked + 24u : 0x80241178u);
    }
    if (!valid) {
        assert(error != NULL);
        assert(strcmp(error, constructor ? "REL constructor raw image changed after validation" :
            "REL owned section table changed after validation") == 0);
        if (host->error != NULL) {
            assert(host->error_pc == 0x803031D8u);
            assert(host->error_address == (strcmp(site, "ownership") == 0 ? linked : target));
        }
    } else {
        if (error != NULL) fprintf(stderr, "STOP reason=%s\n", error);
        assert(error == NULL && host->error == NULL);
    }
    assert(memcmp(&image_before, image, sizeof(*image)) == 0);
    assert(ww_host_rel_raw_hash(raw_before, image->bytes) == raw_hash_before);
    assert(ww_host_rel_raw_hash((const u8*)image->sections,
        image->section_count * sizeof(*image->sections)) == sections_hash_before);
    assert(image->raw_hash == published_hash || (constructor && strcmp(form, "raw-hash") != 0));
    fprintf(stderr, "PASS sections site=%s form=%s %s\n", site, form, valid ? "accepted" : "refused");
}

#endif
