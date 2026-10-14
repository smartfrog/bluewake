#include "probe_support.h"
#include "dispatch_loop.h"
#include <stdlib.h>
#include "ship_fixture.inc"
#include "ship_contract.inc"
#define DOLRECOMP_CPU_HEADER "core/cpu.h"
#define DOLRECOMP_C_LOOP_CYCLE_BUDGET (ctx->cycle_budget)
#include "ship_translated.c"

static const char* scenario;

static bool probe_edge(void* user, CPUState* cpu, u32 address) {
    WwHost* host = user;
    const u32 canonical = address & ~0x40000000u;
    if (canonical == 0x80241178u) {
        if (strcmp(scenario, "ctor-mutated") == 0)
            mem_write32(cpu, 0xC1B1DAF0u, 0x80001004u);
        if (strcmp(scenario, "raw-mutated") == 0) {
            WwRelImage* image = ww_host_rel_live(host, 168u);
            assert(image != NULL);
            mem_write8(cpu, image->guest + 0x1C7u, 1u);
        }
    }
    return ww_host_edge(user, cpu, address);
}

static int translated_chunk(CPUState* cpu, u32 address) {
    switch (address & ~0x40000000u) {
    case 0x80240EC0u: case 0x80240ED8u: case 0x80240EE8u:
        func_80240EC0(cpu); return 1;
    case 0x80241178u: case 0x80241198u: case 0x802411ACu: case 0x802411B4u:
        func_C0241178(cpu); return 1;
    case 0x802410B4u: func_C02410B4(cpu); return 1;
    case 0x81B100F4u: case 0x81B1010Cu: case 0x81B10110u:
        func_C1B100F4(cpu); return 1;
    case 0x81B1D414u: case 0x81B1D424u: case 0x81B1D428u:
        func_C1B1D414(cpu); return 1;
    case 0x81B1016Cu: func_C1B1016C(cpu); return 1;
    default: return 0;
    }
}

static int translated_dispatch(CPUState* cpu, u32 address) {
    return bluewake_chassis_dispatch_loop(cpu, address, translated_chunk, probe_edge, cpu->external_user_data);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRelModule rel = fixture_rel;
    if (strcmp(scenario, "other-actor") == 0)
        rel.module_id = 169u;
    StaticRecompModuleDesc module = {.code_ranges = fixture_ranges, .num_code_ranges = 7u,
        .chunk_ranges = fixture_chunks, .chunk_hashes = fixture_hashes, .num_chunk_ranges = 3u,
        .rel_modules = &rel, .num_rel_modules = 1u, .dispatch = translated_dispatch};
    host->module = &module;
    host->rel_data = fixture_data;
    host->rel_data_count = 2u;
    if (strcmp(scenario, "missing-contract") == 0) {
        const char* error = ww_host_enable_ship_contract(host, NULL, "synthetic.dol");
        assert(error != NULL && host->ship_contract == NULL);
        fprintf(stderr, "PASS missing-contract reason=%s\n", error);
    }
    if (strcmp(scenario, "default-off") != 0 && strcmp(scenario, "missing-contract") != 0) {
        BlueWakeShipContractV1 contract = s_ship_contract;
        static u8 raw[sizeof(s_ship_raw_rel)];
        memcpy(raw, s_ship_raw_rel, sizeof(raw));
        if (strncmp(scenario, "uncertified-", 12u) == 0) {
            const u32 offset = strcmp(scenario, "uncertified-prolog") == 0 ? 0x34u :
                strcmp(scenario, "uncertified-epilog") == 0 ? 0x38u : 0x3Cu;
            write_be32(raw + offset, read_be32(raw + offset) + 4u);
            contract.rel_bytes = raw;
        }
        if (strcmp(scenario, "lifecycle-data") == 0) {
            write_be32(raw + 0x54u, 0x80u);
            contract.rel_bytes = raw;
        }
        static BlueWakeShipContractV1 enabled_contract;
        enabled_contract = contract;
        const char* error = ww_host_enable_ship_contract(host, &enabled_contract, "synthetic.dol");
        if (strcmp(scenario, "bad-dol") == 0) {
            assert(error != NULL && host->ship_contract == NULL);
            fprintf(stderr, "PASS rejected reason=%s\n", error);
            ww_host_shutdown(host);
            free(host);
            return 0;
        }
        assert(error == NULL);
    }
    u64 hashes[3];
    memcpy(hashes, fixture_hashes, sizeof(hashes));
    if (strcmp(scenario, "stale-code") == 0) {
        hashes[0] ^= 1u;
        module.chunk_hashes = hashes;
    }
    for (u32 at = 0u; at < 2u; ++at)
        assert(ppc_guest_alias_add(fixture_data[at].linked_start, fixture_data[at].size, fixture_data[at].bytes));
    CPUState* cpu = &host->cpu;
    cpu->gpr[1] = 0x80004000u;
    cpu->gpr[2] = 0x80300000u;
    cpu->gpr[13] = 0x80400000u;
    cpu->gpr[31] = 0x12345678u;
    cpu->gpr[3] = 0x80002100u;
    cpu->lr = 0x80001004u;
    mem_write32(cpu, 0x8000211Cu, 0x80003000u);
    memcpy(cpu->ram + 0x3000u, strcmp(scenario, "other-actor") == 0 ? "d_a_test" : "d_a_ship", 9u);
    cpu->pc = 0x80240EC0u;
    cpu->cycle_budget = 200;
    for (u32 turn = 0u; turn < 32u && cpu->pc != 0x80001004u && host->error == NULL; ++turn) {
        if ((cpu->pc & ~0x40000000u) == 0x81B100F4u) {
            if (strcmp(scenario, "entry-lr") == 0) cpu->lr = 0x80001004u;
            if (strcmp(scenario, "entry-r31") == 0) cpu->gpr[31] = 0x80002200u;
        }
        int result = ww_host_dispatch(host, cpu->pc);
        if (result == 0) {
            ww_host_fail(host, "synthetic translated dispatcher has no instruction", cpu->pc, cpu->pc);
            break;
        }
    }
    if (host->error != NULL)
        fprintf(stderr, "STOP pc=%08X lr=%08X sp=%08X r31=%08X ctor-owner=%08X reason=%s\n",
                cpu->pc, cpu->lr, cpu->gpr[1], cpu->gpr[31], host->rel_constructor_owner, host->error);
    if (strcmp(scenario, "default-off") == 0 || strcmp(scenario, "missing-contract") == 0 ||
        strcmp(scenario, "bad-rel") == 0 || strcmp(scenario, "stale-code") == 0 ||
        strcmp(scenario, "other-actor") == 0 || strcmp(scenario, "lifecycle-data") == 0 ||
        strncmp(scenario, "uncertified-", 12u) == 0) {
        assert(host->error != NULL && cpu->pc == 0x80240744u);
        assert(mem_read32(cpu, 0x80002110u) == 0u && host->alias_count == 0u);
        fprintf(stderr, "PASS rejected-before-publication pc=%08X\n", cpu->pc);
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    if (strcmp(scenario, "ctor-mutated") == 0 || strcmp(scenario, "raw-mutated") == 0 ||
        strncmp(scenario, "entry-", 6u) == 0) {
        assert(host->error != NULL && mem_read32(cpu, 0xC1B224D0u) == 0u);
        fprintf(stderr, "PASS rejected-before-constructor\n");
        ww_host_shutdown(host);
        free(host);
        return 0;
    }
    assert(host->error == NULL);
    assert(cpu->pc == 0x80001004u && cpu->lr == cpu->pc);
    assert(cpu->gpr[1] == 0x80004000u && cpu->gpr[31] == 0x12345678u);
    assert(cpu->gpr[2] == 0x80300000u && cpu->gpr[13] == 0x80400000u);
    assert(mem_read32(cpu, 0xC1B224D0u) == 2u);
    assert(cpu->ctr == 0xC1B1D414u);
    fprintf(stderr, "PASS caller-returned pc=%08X bss-init-calls=2\n", cpu->pc);
    WwRelImage* image = ww_host_rel_live(host, 168u);
    assert(image != NULL && host->ship_prolog_owner == 0u && host->rel_constructor_owner == 0u);
    if (strcmp(scenario, "resume-without-prolog") == 0) {
        cpu->pc = cpu->lr = 0x80240EE8u;
        cpu->gpr[31] = image->owner;
        assert(ww_host_edge(host, cpu, cpu->pc) && host->error != NULL);
        fprintf(stderr, "PASS rejected reason=%s\n", host->error);
    } else if (strcmp(scenario, "relink") == 0) {
        const u32 prolog = mem_read32(cpu, image->guest + 0x34u);
        assert(ww_host_link_rel(host, image->owner) == NULL);
        assert(mem_read32(cpu, 0xC1B224D0u) == 2u && mem_read32(cpu, image->guest + 0x34u) == prolog);
        assert(ww_host_rel_raw_unchanged(image));
        fprintf(stderr, "PASS relink-preserved\n");
    } else if (strcmp(scenario, "unlink") == 0) {
        const char* error = ww_host_unlink_rel(host, image->owner);
        assert(error != NULL && image->linked && !image->unlinked);
        assert(mem_read32(cpu, image->owner + 0x10u) == image->guest);
        fprintf(stderr, "PASS rejected reason=%s\n", error);
    } else if (strncmp(scenario, "epilog-", 7u) == 0 || strncmp(scenario, "unresolved-", 11u) == 0 ||
               strcmp(scenario, "native-unlink") == 0) {
        const bool epilog = strncmp(scenario, "epilog-", 7u) == 0;
        cpu->pc = epilog ? 0xC1B10120u : 0xC1B1014Cu;
        if (strstr(scenario, "scratch") != NULL)
            cpu->pc = mem_read32(cpu, image->guest + (epilog ? 0x38u : 0x3Cu));
        if (strcmp(scenario, "native-unlink") == 0) {
            cpu->pc = 0x80240F54u;
            cpu->gpr[3] = image->owner;
        }
        assert(ww_host_dispatch(host, cpu->pc) == -1 && host->error != NULL && image->linked);
        fprintf(stderr, "PASS rejected reason=%s\n", host->error);
    } else if (strncmp(scenario, "return-", 7u) == 0) {
        cpu->lr = 0x80240EE8u;
        cpu->gpr[31] = image->owner;
        assert(ww_host_ship_boundary(host, 0xC1B100F4u) == NULL);
        if (strcmp(scenario, "return-without-ctor") != 0) {
            cpu->gpr[3] = 0xC1B1DAF0u;
            cpu->lr = 0xC1B1010Cu;
            assert(ww_host_rel_constructor_boundary(host, 0x80241178u) == NULL);
            assert(ww_host_rel_constructor_boundary(host, 0xC1B1010Cu) == NULL);
            cpu->lr = 0x80240EE8u;
        }
        if (strcmp(scenario, "return-sp") == 0) cpu->gpr[1] += 16u;
        if (strcmp(scenario, "return-r31") == 0) cpu->gpr[31] += 32u;
        if (strcmp(scenario, "return-lr") == 0) cpu->lr = 0x80001004u;
        if (strcmp(scenario, "return-r2") == 0) cpu->gpr[2] += 4u;
        if (strcmp(scenario, "return-r13") == 0) cpu->gpr[13] += 4u;
        const char* error = ww_host_ship_boundary(host, 0x80240EE8u);
        assert(error != NULL && host->ship_prolog_owner == image->owner);
        fprintf(stderr, "PASS rejected reason=%s\n", error);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
