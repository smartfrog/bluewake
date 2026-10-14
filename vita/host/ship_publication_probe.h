#ifndef WW_SHIP_PUBLICATION_PROBE_H
#define WW_SHIP_PUBLICATION_PROBE_H
#include "rel_sdk_fixture.h"

static void relocate_ship_payload(WwHost* host, const WwRelImage* image) {
    ww_probe_sdk_append(host, image, image->sdk_fixed);
}

static void seed_ship_profile(WwHost* host) {
    static StaticRecompModuleDesc module;
    static StaticRecompRelModule descriptors[2];
    static BlueWakeRelData data[3];
    static const u8 profile_bytes[4];
    static const StaticRecompRelSection profile_section = {1u,4u,0xC1F80178u,4u};
    module = *host->module;
    descriptors[0] = host->module->rel_modules[0];
    descriptors[1] = (StaticRecompRelModule){.module_id=1u,.sections=&profile_section,.num_sections=1u};
    module.rel_modules = descriptors;
    module.num_rel_modules = 2u;
    memcpy(data, host->rel_data, 2u * sizeof(*data));
    data[2] = (BlueWakeRelData){1u,4u,0xC1F80178u,4u,profile_bytes};
    host->module = &module;
    host->rel_data = data;
    host->rel_data_count = 3u;
    assert(ppc_guest_alias_add(0x81822E80u, 32u, NULL));
    assert(ppc_guest_alias_add(data[2].linked_start, data[2].size, data[2].bytes));
    host->rel_images[0] = (WwRelImage){.owner=0x80002400u,.guest=0x81822E80u,.bytes=32u,.capacity=32u,
                                     .module_id=1u,.linked=true};
    host->rel_image_count = 1u;
    mem_write32(&host->cpu, 0x81822E80u, 1u);
    mem_write32(&host->cpu, 0x803F6A68u, data[2].linked_start);
    mem_write32(&host->cpu, 0x800030C8u, 0x81822E80u);
    mem_write32(&host->cpu, 0x800030CCu, 0x81822E80u);
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
        seed_ship_profile(host);
    }
    const u32 owner = cpu->gpr[3];
    const char* materialize_error = ww_host_materialize_rel(host, owner, "d_a_ship");
    if (materialize_error != NULL) fprintf(stderr, "publication materialize: %s\n", materialize_error);
    assert(materialize_error == NULL);
    WwRelImage* image = ww_host_rel_owned(host, owner);
    assert(image != NULL && image->guest == WW_REL_SHIP_SCRATCH_BASE);
    const u64 materialized_hash = image->raw_hash;
    assert(ww_host_link_rel(host, owner) == NULL);
    assert(ww_host_rel_live(host, 168u) == image);
    if (sdk_entry || strcmp(scenario, "publication-entry-pc") == 0) {
        if (sdk_entry) {
            mem_write32(cpu, image->owner + 0x14u, 0x807FF620u);
            assert(ww_host_rel_sdk_prepare(host, image,
                strcmp(scenario, "publication-sdk-entry-unfixed") != 0) == NULL);
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
        if (strcmp(scenario, "publication-sdk-entry-ctor") == 0) {
            cpu->lr = image->sections[1].linked_start + 24u;
            cpu->gpr[3] = host->rel_data[0].linked_start;
            const char* error = ww_host_rel_constructor_boundary(host, 0x80241178u);
            assert(error != NULL && strcmp(error, "REL constructor raw image changed after validation") == 0);
            assert(image->raw_hash == materialized_hash && host->rel_constructor_owner == 0u);
            fprintf(stderr, "PASS SDK constructor without entry refused\n");
            return;
        }
        const bool after_tamper = strcmp(scenario, "publication-sdk-entry-after-tamper") == 0;
        const bool accepted = strcmp(scenario, "publication-sdk-entry") == 0 ||
            strcmp(scenario, "publication-sdk-entry-unfixed") == 0 || after_tamper;
        const char* error = NULL;
        if (accepted) {
            assert(cpu->host_call(cpu, cpu->pc));
            error = host->error;
        } else {
            error = ww_host_ship_boundary(host, entry);
        }
        if (accepted) {
            if (error != NULL) fprintf(stderr, "STOP SDK entry reason=%s pristine=%016llX live=%016llX\n",
                error, (unsigned long long)materialized_hash, (unsigned long long)live_hash);
            assert(error == NULL && host->ship_prolog_owner == owner && !host->ship_prolog_return_ready);
            assert(ww_host_rel_raw_unchanged(image));
            assert(cpu->pc == entry);
            assert(ww_host_ship_boundary(host, entry) == NULL);
            if (after_tamper) {
                const u64 admitted_hash = image->raw_hash;
                mem_write8(cpu, image->guest + 0x14u, mem_read8(cpu, image->guest + 0x14u) ^ 1u);
                error = ww_host_ship_boundary(host, entry + 24u);
                assert(error != NULL && strcmp(error, "ship prolog continuation lost its exact live owner") == 0);
                assert(image->raw_hash == admitted_hash && !ww_host_rel_raw_unchanged(image));
                fprintf(stderr, "PASS SDK continuation tampering refused\n");
                return;
            }
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
