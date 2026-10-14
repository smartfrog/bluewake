#include "probe_support.h"
#include <stdlib.h>
#include "ship_fixture.inc"
#include "ship_contract.inc"
#include "rel_contract_fixture.inc"
#include "rel_sdk_fixture.h"

static void legacy_terminator(WwHost* host) {
    BlueWakeShipContractV1 contract = s_ship_contract;
    BlueWakeRelData sections[256];
    memcpy(sections, contract.canonical_sections, contract.section_count * sizeof(*sections));
    static const u8 unterminated[] = {0xC1, 0xB1, 0xD4, 0x14, 0xC1, 0xB1, 0xD4, 0x14,
                                    0xC1, 0xB1, 0xD4, 0x14};
    for (u32 at = 0u; at < contract.section_count; ++at)
        if (sections[at].section_index == 4u)
            sections[at].bytes = unterminated;
    contract.canonical_sections = sections;
    assert(ww_host_enable_ship_contract(host, &contract, "synthetic.dol") != NULL);
    assert(host->ship_contract == NULL);
}

static WwRelImage* publish(WwHost* host, u32 owner, const char* name) {
    assert(ww_host_materialize_rel(host, owner, name) == NULL);
    host->cpu.lr = 0x80001004u;
    assert(ww_host_link_rel(host, owner) == NULL);
    WwRelImage* image = ww_host_rel_owned(host, owner);
    assert(image != NULL && ww_host_rel_live(host, image->module_id) == image);
    return image;
}

static void change_schema(const char* field, BlueWakeRelInitContractSetV1* set,
                          BlueWakeRelInitContractV1* certificates, BlueWakeRelData* canonical,
                          BlueWakeRelData* data, BlueWakeRelLifecycleV2* lifecycle, u8* raw, u8* code, u8* ctors) {
    if (strcmp(field, "version") == 0) set->version = 2u;
    else if (strcmp(field, "count-zero") == 0) set->contract_count = 0u;
    else if (strcmp(field, "count-limit") == 0) set->contract_count = WW_REL_MAX_IMAGES + 1u;
    else if (strcmp(field, "null") == 0) set->contracts = NULL;
    else if (strcmp(field, "identity") == 0) set->build_identity = "unapproved";
    else if (strcmp(field, "duplicate") == 0) certificates[1].module_id = certificates[0].module_id;
    else if (strcmp(field, "unknown-id") == 0) certificates[1].module_id = 169u;
    else if (strcmp(field, "canonical-count") == 0) certificates[1].section_count = 1u;
    else if (strcmp(field, "canonical-duplicate") == 0) canonical[1] = canonical[0];
    else if (strcmp(field, "canonical-code") == 0) { code[0] ^= 1u; canonical[0].bytes = code; }
    else if (strcmp(field, "export-duplicate") == 0) data[3] = data[2];
    else if (strcmp(field, "lifecycle-id") == 0) lifecycle->module_id = 168u;
    else if (strcmp(field, "lifecycle-selector") == 0) raw[0x30u] = 7u;
    else if (strcmp(field, "lifecycle-offset") == 0) write_be32(raw + 0x34u, 4u);
    else if (strcmp(field, "ctor-terminator") == 0) {
        write_be32(ctors, 0xC1E000DCu);
        write_be32(raw + 0xC0u, 0xC1E000DCu);
        canonical[1].bytes = ctors;
        data[2].bytes = ctors;
    } else if (strcmp(field, "imports") == 0) write_be32(raw + 0xD0u, 168u);
    else if (strcmp(field, "relocation") == 0) raw[0xEAu] = 11u;
    else if (strcmp(field, "stream-end") == 0) raw[0xF2u] = 201u;
    else if (strcmp(field, "zero-bss") == 0) write_be32(raw + 0x20u, 4u);
    else if (strcmp(field, "zero-bss-selector") == 0) raw[0x33u] = 7u;
    else if (strcmp(field, "empty-flags") == 0) write_be32(raw + 0x84u, 1u);
    else if (strcmp(field, "empty-size") == 0) write_be32(raw + 0x88u, 4u);
    else if (strcmp(field, "alignment") == 0) write_be32(raw + 0x40u, 3u);
    else if (strcmp(field, "rel-version") == 0) write_be32(raw + 0x1Cu, 2u);
    else if (strcmp(field, "fabricated-export") == 0) data[3] = (BlueWakeRelData){400u,7u,0xC1E02000u,4u,NULL};
    else assert(false);
}

static void change_append(WwHost* host, const char* field, WwRelImage* profile, WwRelImage* ship, WwRelImage* title) {
    CPUState* cpu = &host->cpu;
    if (strcmp(field, "head") == 0) mem_write32(cpu, 0x800030C8u, ship->guest);
    else if (strcmp(field, "tail") == 0) mem_write32(cpu, 0x800030CCu, ship->guest);
    else if (strcmp(field, "previous") == 0) mem_write32(cpu, title->guest + 8u, profile->guest);
    else if (strcmp(field, "next") == 0) mem_write32(cpu, title->guest + 4u, ship->guest);
    else if (strcmp(field, "cycle") == 0) mem_write32(cpu, ship->guest + 4u, profile->guest);
    else if (strcmp(field, "skipped") == 0) mem_write32(cpu, profile->guest + 4u, title->guest);
    else if (strcmp(field, "candidate-owner") == 0) mem_write32(cpu, title->owner + 0x10u, ship->guest);
    else if (strcmp(field, "neighbor-owner") == 0) mem_write32(cpu, ship->owner + 0x10u, title->guest);
    else if (strcmp(field, "profile-global") == 0) mem_write32(cpu, 0x803F6A68u, 0u);
    else if (strcmp(field, "neighbor-write") == 0) mem_write8(cpu, ship->guest + ship->bytes - 1u, 1u);
    else if (strcmp(field, "name") == 0) mem_write32(cpu, 0x800030D0u, 0x80001000u);
    else if (strcmp(field, "bss") == 0) mem_write32(cpu, title->owner + 0x14u, 0x80003000u);
    else if (strcmp(field, "import-size") == 0) mem_write32(cpu, title->guest + 0x2Cu, 16u);
    else if (strcmp(field, "empty-flags") == 0) mem_write32(cpu, title->guest + 0x84u, 1u);
    else if (strcmp(field, "empty-size") == 0) mem_write32(cpu, title->guest + 0x88u, 4u);
    else if (strcmp(field, "zero-bss-selector") == 0) mem_write8(cpu, title->guest + 0x33u, 7u);
    else if (strcmp(field, "zero-bss") == 0) mem_write32(cpu, title->guest + 0x20u, 4u);
    else if (strcmp(field, "lifecycle-selector") == 0) mem_write8(cpu, title->guest + 0x30u, 0u);
    else if (strcmp(field, "ctor-terminator") == 0) mem_write32(cpu, title->guest + 0xC0u, 0xC1E000DCu);
    else if (strcmp(field, "partial") == 0) mem_write32(cpu, title->guest + 0x48u, 0xC4u);
    else assert(false);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRelSection profile_sections[] = {{1u,1u,0xC1F80000u,32u}, {1u,4u,0xC1F80178u,16u}};
    StaticRecompRelModule descriptors[] = {fixture_rel,
        {.module_id=400u,.section_count=8u,.section_info_offset=0x4Cu,.file_size=sizeof(title_raw),
         .sections=title_sections,.num_sections=8u},
        {.module_id=1u,.section_count=5u,.section_info_offset=0x4Cu,.file_size=256u,
         .sections=profile_sections,.num_sections=2u}};
    StaticRecompRange ranges[32];
    memcpy(ranges, fixture_ranges, sizeof(fixture_ranges));
    u32 range_count = sizeof(fixture_ranges) / sizeof(fixture_ranges[0]);
    ranges[range_count++] = (StaticRecompRange){0xC1E000DCu,0xC1E000FCu};
    ranges[range_count++] = (StaticRecompRange){0x80305424u,0x8030544Cu};
    StaticRecompRange chunks[4];
    memcpy(chunks, fixture_chunks, sizeof(fixture_chunks));
    chunks[3] = (StaticRecompRange){0xC1E000DCu,0xC1E000FCu};
    u64 hashes[] = {fixture_hashes[0],fixture_hashes[1],fixture_hashes[2],title_hash};
    StaticRecompModuleDesc module = {.code_ranges = ranges, .num_code_ranges = range_count,
        .chunk_ranges = chunks, .chunk_hashes = hashes, .num_chunk_ranges = 4u,
        .rel_modules = descriptors, .num_rel_modules = 3u};
    host->module = &module;
    static const u8 profile_bytes[16];
    BlueWakeRelData data[] = {fixture_data[0],fixture_data[1],title_canonical[1],
                            {1u,4u,0xC1F80178u,16u,profile_bytes}};
    host->rel_data = data;
    host->rel_data_count = 4u;
    if (strcmp(argv[1], "legacy-ctor-terminator") == 0) {
        legacy_terminator(host);
    } else {
        static const char identity[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        BlueWakeRelLifecycleV2 ship_lifecycle = {168u,0xC1B100F4u,0xC1B10120u,0xC1B1014Cu};
        u8 raw[sizeof(title_raw)], code[sizeof(title_code)], ctors[sizeof(title_ctors)];
        memcpy(raw, title_raw, sizeof(raw));
        memcpy(code, title_code, sizeof(code));
        memcpy(ctors, title_ctors, sizeof(ctors));
        BlueWakeRelData canonical[] = {title_canonical[0],title_canonical[1]};
        BlueWakeRelLifecycleV2 lifecycle = title_lifecycle;
        BlueWakeRelInitContractV1 certificates[] = {
            {.module_id=168u,.rel_size=s_ship_contract.rel_size,.rel_bytes=s_ship_contract.rel_bytes,
             .canonical_sections=s_ship_contract.canonical_sections,.section_count=s_ship_contract.section_count,
             .ctor_table_section=4u,.ctor_return_pc=0xC1B1010Cu,.lifecycle=&ship_lifecycle,.build_identity=identity},
            {.module_id=400u,.rel_size=sizeof(raw),.rel_bytes=raw,.canonical_sections=canonical,
             .section_count=2u,.ctor_table_section=2u,.ctor_return_pc=0xC1E000F4u,
             .lifecycle=&lifecycle,.build_identity=identity}};
        BlueWakeRelInitContractSetV1 set = {1u,s_ship_contract.dol_size,s_ship_contract.dol_bytes,
                                           certificates,2u,identity};
        const bool schema = strncmp(argv[1], "schema-", 7u) == 0;
        if (schema)
            change_schema(argv[1] + 7u, &set, certificates, canonical, data, &lifecycle, raw, code, ctors);
        StaticRecompRelSection catalog_sections[8];
        if (strcmp(argv[1], "catalog-id") == 0) {
            write_be32(raw, 401u);
            write_be32(raw + 0xD0u, 401u);
            certificates[1].module_id = 401u;
            lifecycle.module_id = 401u;
            descriptors[1].module_id = 401u;
            memcpy(catalog_sections, title_sections, sizeof(catalog_sections));
            for (u32 at = 0u; at < 8u; ++at) catalog_sections[at].module_id = 401u;
            descriptors[1].sections = catalog_sections;
            for (u32 at = 0u; at < 2u; ++at) canonical[at].module_id = 401u;
            data[2].module_id = 401u;
        }
        if (strcmp(argv[1], "uncertified") == 0)
            set.contract_count = 1u;
        const bool default_off = strcmp(argv[1], "default-off") == 0;
        const char* error = default_off ? NULL : ww_host_register_rel_contracts(host, &set, "synthetic.dol");
        if (schema) {
            assert(error != NULL && host->rel_contracts == NULL);
            ww_host_shutdown(host);
            free(host);
            return 0;
        }
        if (error != NULL) fprintf(stderr, "registration: %s\n", error);
        assert(error == NULL);
        if (strcmp(argv[1], "ship-only") == 0)
            assert(ww_host_enable_ship_contract(host, &s_ship_contract, "synthetic.dol") == NULL);
        if (default_off || strcmp(argv[1], "registration-only") == 0 || strcmp(argv[1], "ship-only") == 0) {
            assert(ww_host_materialize_rel(host, 0x80002300u, "d_a_title") != NULL);
            assert(ww_host_rel_owned(host, 0x80002300u) == NULL);
        } else {
            assert(ww_host_rel_test_admission(host) == NULL);
            if (strcmp(argv[1], "uncertified") == 0 || strcmp(argv[1], "raw-identity") == 0) {
                assert(ww_host_materialize_rel(host, 0x80002300u, "d_a_title") != NULL);
                assert(ww_host_rel_owned(host, 0x80002300u) == NULL);
                ww_host_shutdown(host);
                free(host);
                return 0;
            }
            for (u32 at = 0u; at < 4u; ++at)
                assert(ppc_guest_alias_add(data[at].linked_start, data[at].size, data[at].bytes));
            WwRelImage* profile = strcmp(argv[1], "empty-list") == 0 ? NULL : publish(host, 0x80002100u, "f_pc_profile_lst");
            mem_write32(&host->cpu, 0x800030C8u, profile == NULL ? 0u : profile->guest);
            mem_write32(&host->cpu, 0x800030CCu, profile == NULL ? 0u : profile->guest);
            if (strcmp(argv[1], "profile-owner-expired") == 0)
                mem_write32(&host->cpu, profile->owner + 0x10u, 0xA5A5A5A5u);
            WwRelImage* ship = publish(host, 0x80002200u, "d_a_ship");
            assert(ship->certificate == &certificates[0]);
            mem_write32(&host->cpu, ship->owner + 0x14u, 0x807FF620u);
            if (strcmp(argv[1], "seed-missing-profile-list") == 0) {
                mem_write32(&host->cpu, 0x800030C8u, 0u);
                mem_write32(&host->cpu, 0x800030CCu, 0u);
                assert(ww_host_rel_sdk_prepare(host, ship, true) != NULL);
                assert(!host->rel_sdk_seeded && profile->sdk_phase == WW_REL_SDK_NONE);
                assert(ship->sdk_phase == WW_REL_SDK_NONE && ww_host_rel_raw_unchanged(ship));
                ww_host_shutdown(host);
                free(host);
                return 0;
            }
            if (strcmp(argv[1], "owner-wrong-image") == 0) {
                assert(ww_host_materialize_rel(host, ship->owner, "d_a_title") != NULL);
                assert(ww_host_rel_owned(host, ship->owner) == ship && ship->certificate == &certificates[0]);
                ww_host_shutdown(host);
                free(host);
                return 0;
            }
            if (strcmp(argv[1], "entry-observation") == 0) {
                host->cpu.pc = 0x80305448u;
                host->cpu.gpr[3] = ship->guest;
                host->cpu.gpr[4] = 0x807FF620u;
                const CPUState before = host->cpu;
                assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
                assert(memcmp(&before, &host->cpu, sizeof(before)) == 0);
                assert(ship->sdk_phase == WW_REL_SDK_PENDING && ship->sdk_bss == 0x807FF620u);
                assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
                assert(memcmp(&before, &host->cpu, sizeof(before)) == 0);
            } else {
                assert(ww_host_rel_sdk_prepare(host, ship, true) == NULL);
            }
            if (strcmp(argv[1], "pending-replacement") == 0) {
                WwRelImage* title = publish(host, 0x80002300u, "d_a_title");
                assert(ww_host_rel_sdk_prepare(host, title, true) != NULL);
                assert(host->rel_sdk_pending_owner == ship->owner && title->sdk_phase == WW_REL_SDK_NONE);
                ww_host_shutdown(host);
                free(host);
                return 0;
            }
            ww_probe_sdk_append(host, ship, true);
            assert(ww_host_rel_sdk_finish(host, ship) == NULL);
            assert(ww_host_rel_sdk_integrity(host));
            const u64 ship_hash = ship->raw_hash;
            WwRelImage* title = publish(host, 0x80002300u, "d_a_title");
            assert(title->certificate == &certificates[1]);
            assert(ww_host_rel_sdk_prepare(host, title, true) == NULL);
            if (strcmp(argv[1], "pending-wrong-image") == 0) {
                WwRelImage forged = *title;
                assert(ww_host_rel_sdk_finish(host, &forged) != NULL);
                assert(host->rel_sdk_pending_owner == title->owner && !host->rel_sdk_failed);
                assert(title->sdk_phase == WW_REL_SDK_PENDING && ship->raw_hash == ship_hash);
                ww_host_shutdown(host);
                free(host);
                return 0;
            }
            const bool wrong_mode = strcmp(argv[1], "fixed-mismatch") == 0 || strcmp(argv[1], "icbi-before-trim") == 0;
            ww_probe_sdk_append(host, title, !wrong_mode);
            if (strcmp(argv[1], "icbi-before-trim") == 0) {
                assert(ww_host_rel_icbi_allowed(host, title->guest + 0xA0u));
                assert(title->sdk_phase == WW_REL_SDK_PENDING && title->raw_hash != ship->raw_hash);
                assert(!ww_host_rel_raw_unchanged(title));
                mem_write32(&host->cpu, title->guest + 0x2Cu, 0u);
            }
            const bool rollback = strncmp(argv[1], "rollback-", 9u) == 0 || strcmp(argv[1], "fixed-mismatch") == 0;
            if (rollback) {
                if (!wrong_mode) change_append(host, argv[1] + 9u, profile, ship, title);
                const u64 title_hash = title->raw_hash;
                assert(ww_host_rel_sdk_finish(host, title) != NULL);
                assert(host->rel_sdk_head == profile->guest && host->rel_sdk_tail == ship->guest);
                assert(ship->raw_hash == ship_hash && ship->sdk_next == 0u && title->raw_hash == title_hash);
                assert(profile->sdk_next == ship->guest && host->rel_sdk_pending_owner == title->owner);
                assert(title->sdk_phase == WW_REL_SDK_FAILED && host->rel_sdk_failed);
                assert(!ww_host_rel_sdk_integrity(host) && ww_host_rel_sdk_finish(host, title) != NULL);
                ww_host_shutdown(host);
                free(host);
                return 0;
            }
            assert(ww_host_rel_sdk_finish(host, title) == NULL);
            assert(ship->raw_hash != ship_hash && ww_host_rel_raw_unchanged(ship));
            assert(ww_host_rel_raw_unchanged(title) && ww_host_rel_sdk_integrity(host));
            if (profile != NULL)
                assert(profile->sdk_next == ship->guest && ship->sdk_previous == profile->guest);
            else
                assert(host->rel_sdk_head == ship->guest && ship->sdk_previous == 0u);
            assert(ship->sdk_next == title->guest && title->sdk_previous == ship->guest && title->sdk_next == 0u);
            assert(ww_host_unlink_rel(host, title->owner) != NULL && title->linked);
            if (strcmp(argv[1], "append-tamper") == 0) {
                mem_write8(&host->cpu, ship->guest + 0x14u, 1u);
                assert(!ww_host_rel_sdk_integrity(host) && !ww_host_rel_raw_unchanged(ship));
            } else if (strncmp(argv[1], "execution-closed-", 17u) == 0) {
                if (strcmp(argv[1] + 17u, "prolog") == 0) host->cpu.pc = title_lifecycle.prolog_address;
                else if (strcmp(argv[1] + 17u, "epilog") == 0) host->cpu.pc = title_lifecycle.epilog_address;
                else { assert(strcmp(argv[1] + 17u, "unresolved") == 0); host->cpu.pc = title_lifecycle.unresolved_address; }
                assert(host->cpu.host_call(&host->cpu, host->cpu.pc));
                assert(host->error != NULL && host->ship_prolog_owner == 0u);
            } else {
                assert(strcmp(argv[1], "selected-append") == 0 || strcmp(argv[1], "catalog-id") == 0 ||
                       strcmp(argv[1], "entry-observation") == 0 || strcmp(argv[1], "icbi-before-trim") == 0 ||
                       strcmp(argv[1], "profile-owner-expired") == 0 || strcmp(argv[1], "empty-list") == 0);
            }
        }
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
