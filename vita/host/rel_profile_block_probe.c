#include "probe_support.h"
#include <stdarg.h>
#include <stdlib.h>
#include "ship_fixture.inc"
#include "ship_contract.inc"
#define DOLRECOMP_CPU_HEADER "core/cpu.h"
#define DOLRECOMP_C_LOOP_CYCLE_BUDGET (ctx->cycle_budget)
#include "profile_translated.c"
#include "rel_profile_block_fixture.inc"

static bool allocation_failure;
static bool trace_failure;

void* __real_malloc(size_t size);
void* __wrap_malloc(size_t size) {
    return allocation_failure ? NULL : __real_malloc(size);
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (trace_failure && (strstr(format, "[rel-profile") != NULL || strstr(format, "[rel-sdk]") != NULL))
        return -1;
    va_list args;
    va_start(args, format);
    const int result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

static int translated_dispatch(CPUState* cpu, u32 address) {
    const u32 canonical = address & ~0x40000000u;
    for (u32 at = 0u; at < sizeof(profile_chunks) / sizeof(profile_chunks[0]); ++at) {
        if (canonical >= profile_chunks[at].start && canonical < profile_chunks[at].end) {
            profile_chunks[at].run(cpu);
            return 1;
        }
    }
    return 0;
}

static void at(WwHost* host, WwRelImage* profile, u32 pc, u32 caller) {
    host->cpu.pc = pc;
    host->cpu.lr = caller;
    host->cpu.gpr[3] = profile->guest;
    host->cpu.gpr[4] = 0u;
    host->cpu.gpr[5] = 0u;
    host->cpu.gpr[31] = profile->owner;
}

static void run_until(WwHost* host, u32 stop) {
    for (u32 turn = 0u; turn < 256u && host->error == NULL && host->cpu.pc != stop; ++turn) {
        ww_host_end_dispatch(host);
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount = 0;
        host->cpu.cycle_budget = 1000;
        const u32 address = host->cpu.pc;
        if (!ww_host_edge(host, &host->cpu, address))
            assert(ww_host_dispatch(host, address) != 0 || host->error != NULL);
    }
    if (host->error != NULL) fprintf(stderr, "probe: %s pc=%08X\n", host->error, host->cpu.pc);
}

static void reach(WwHost* host, u32 stop) {
    run_until(host, stop);
    assert(host->error == NULL && host->cpu.pc == stop);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRelModule descriptors[] = {fixture_rel,
        {.module_id=1u,.section_count=5u,.section_info_offset=0x4Cu,.file_size=1024u,
         .sections=resident_sections,.num_sections=3u}};
    StaticRecompRange chunks[] = {fixture_chunks[0],fixture_chunks[1],fixture_chunks[2],
                                 {0xC1F800D4u,0xC1F80100u}};
    u64 hashes[] = {fixture_hashes[0],fixture_hashes[1],fixture_hashes[2],profile_code_hash};
    StaticRecompModuleDesc module = {.code_ranges=profile_ranges,
        .num_code_ranges=sizeof(profile_ranges)/sizeof(profile_ranges[0]),.chunk_ranges=chunks,
        .chunk_hashes=hashes,.num_chunk_ranges=4u,.rel_modules=descriptors,.num_rel_modules=2u,
        .dispatch=translated_dispatch};
    BlueWakeRelData data[] = {fixture_data[0],fixture_data[1],
        {1u,2u,0xC1F80170u,4u,resident_ctors},{1u,4u,0xC1F80178u,16u,resident_data}};
    host->module = &module;
    host->rel_data = data;
    host->rel_data_count = 4u;
    const bool off = strcmp(scenario, "policy-off") == 0;
    const bool title = strcmp(scenario, "policy-title") == 0;
    const bool ship = strcmp(scenario, "policy-ship") == 0;
    if (!off && !title)
        assert(ww_host_enable_ship_contract(host, &s_ship_contract, "synthetic.dol") == NULL);
    host->rel_title_admission = !off && !ship;
    for (u32 index = 0u; index < 4u; ++index)
        assert(ppc_guest_alias_add(data[index].linked_start, data[index].size, data[index].bytes));
    assert(ww_host_materialize_rel(host, 0x80002400u, "f_pc_profile_lst") == NULL);
    WwRelImage* profile = ww_host_rel_owned(host, 0x80002400u);
    assert(profile != NULL);
    host->cpu.lr = 0x80001004u;
    allocation_failure = strcmp(scenario, "failure-allocation") == 0;
    trace_failure = strcmp(scenario, "failure-trace-entry") == 0;
    const char* error = ww_host_link_rel(host, profile->owner);
    allocation_failure = false;
    trace_failure = false;
    if (strncmp(scenario, "failure-", 8u) == 0 &&
        (strcmp(scenario + 8u, "allocation") == 0 || strcmp(scenario + 8u, "trace-entry") == 0)) {
        assert(error != NULL && !profile->initialization_completed && profile->certificate == NULL);
        goto done;
    }
    assert(error == NULL);
    const u64 original_hash = profile->raw_hash;
    at(host, profile, 0x802B07ECu, 0x80240CC4u);
    host->cpu.gpr[1] = 0x80004000u;
    host->cpu.gpr[2] = 0x80300000u;
    host->cpu.gpr[13] = 0x80400000u;
    if (off || title || ship || strcmp(scenario, "ordinary") == 0) {
        if (strcmp(scenario, "ordinary") == 0) host->cpu.gpr[3] = 0x80006000u;
        const CPUState before = host->cpu;
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
        assert(memcmp(&before, &host->cpu, sizeof(before)) == 0);
        assert(profile->raw_hash == original_hash && ww_host_rel_raw_unchanged(profile));
        goto done;
    }
    if (strcmp(scenario, "forged-seed") == 0) {
        mem_write32(&host->cpu, 0x800030C8u, profile->guest);
        mem_write32(&host->cpu, 0x800030CCu, profile->guest);
        assert(ww_host_materialize_rel(host, 0x80002200u, "d_a_ship") == NULL);
        host->cpu.lr = 0x80001004u;
        assert(ww_host_link_rel(host, 0x80002200u) == NULL);
        WwRelImage* candidate = ww_host_rel_owned(host, 0x80002200u);
        mem_write32(&host->cpu, candidate->owner + 0x14u, 0x807FF620u);
        assert(ww_host_rel_sdk_prepare(host, candidate, false) != NULL);
        assert(!host->rel_sdk_seeded && profile->sdk_phase == WW_REL_SDK_NONE);
        goto done;
    }
    if (strncmp(scenario, "route-", 6u) == 0 || strcmp(scenario, "pending-exception") == 0) {
        const u32 guest = profile->guest;
        host->cpu.cycle_budget = 1000;
        host->cpu.downcount = 0;
        if (strcmp(scenario, "route-host") == 0)
            assert(host->cpu.host_call(&host->cpu, host->cpu.pc));
        else if (strcmp(scenario, "route-edge") == 0)
            assert(ww_host_edge(host, &host->cpu, host->cpu.pc));
        else if (strcmp(scenario, "route-dispatch") == 0)
            assert(ww_host_dispatch(host, host->cpu.pc) == 1);
        else {
            host->cpu.exception = PPC_EXC_PROGRAM;
            const CPUState before = host->cpu;
            assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
            assert(memcmp(&before, &host->cpu, sizeof(before)) == 0);
            assert(profile->raw_hash == original_hash);
            goto done;
        }
        assert(host->error == NULL && !host->edge_stopped && profile->guest == guest);
        assert(host->cpu.pc == 0x80240CC4u && host->cpu.gpr[3] == profile->capacity);
        assert(host->cpu.downcount == 0 && profile->raw_hash == original_hash);
        goto done;
    }
    if (strncmp(scenario, "ownership-", 10u) == 0 && strcmp(scenario + 10u, "resize") != 0 &&
        strcmp(scenario + 10u, "names") != 0) {
        const char* field = scenario + 10u;
        if (strcmp(field, "interior") == 0) host->cpu.gpr[3] += 4u;
        else if (strcmp(field, "owner") == 0) mem_write32(&host->cpu, profile->owner + 0x10u, 0u);
        else if (strcmp(field, "global") == 0) mem_write32(&host->cpu, 0x803F6A68u, 0u);
        else if (strcmp(field, "ambiguous") == 0) {
            host->rel_images[1] = *profile;
            host->rel_images[1].sections = NULL;
            host->rel_image_count = 2u;
        } else if (strcmp(field, "extent") == 0) mem_write32(&host->cpu, profile->guest + 0x48u, UINT32_MAX);
        else { assert(strcmp(field, "raw") == 0); mem_write8(&host->cpu, profile->guest + 1023u, 1u); }
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc));
        assert(host->error != NULL && profile->raw_hash == original_hash);
        goto done;
    }
    if (strcmp(scenario, "sequence-constructor") == 0 || strcmp(scenario, "sequence-return") == 0) {
        host->cpu.pc = strcmp(scenario, "sequence-return") == 0 ? 0x80240EE8u : 0xC0241178u;
        host->cpu.lr = 0xC1F800ECu;
        host->cpu.gpr[3] = 0xC1F80170u;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    host->cpu.pc = 0x80240C9Cu;
    reach(host, 0x80305448u);
    assert(mem_read32(&host->cpu, host->cpu.gpr[13] - 27060u) == 0u);
    assert(host->cpu.gpr[4] == profile->guest + 0xE0u);
    assert(mem_read32(&host->cpu, profile->owner + 0x14u) == 0u);
    if (strncmp(scenario, "sequence-sdk-", 13u) == 0 || strcmp(scenario, "sequence-normal") == 0) {
        if (strcmp(scenario, "sequence-normal") == 0) host->cpu.pc = 0x80305424u;
        else if (strcmp(scenario + 13u, "argument") == 0) host->cpu.gpr[4] += 4u;
        else if (strcmp(scenario + 13u, "module") == 0) host->cpu.gpr[3] = 0x80006000u;
        else if (strcmp(scenario + 13u, "caller") == 0) host->cpu.lr += 4u;
        else if (strcmp(scenario + 13u, "result") == 0) {
            reach(host, 0x80240D4Cu);
            host->cpu.gpr[3] = 0u;
        } else {
            assert(strcmp(scenario + 13u, "reentry") == 0);
            assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
            host->cpu.pc = 0x8000A000u;
            assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
            host->cpu.pc = 0x80305448u;
        }
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    if (strcmp(scenario, "duplicate") == 0) assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    reach(host, 0x80240D4Cu);
    assert(mem_read32(&host->cpu, 0x800030C8u) == profile->guest);
    assert(mem_read32(&host->cpu, 0x800030CCu) == profile->guest);
    assert(!host->rel_sdk_seeded && profile->certificate == NULL && profile->sdk_phase == WW_REL_SDK_NONE);
    if (strncmp(scenario, "image-", 6u) == 0) {
        const char* field = scenario + 6u;
        u32 offset = strcmp(field, "header") == 0 ? 0x14u : strcmp(field, "section") == 0 ? 0x54u :
            strcmp(field, "relocation") == 0 ? 0xB4u : strcmp(field, "foreign") == 0 ? 0xBCu : 1023u;
        mem_write8(&host->cpu, profile->guest + offset, mem_read8(&host->cpu, profile->guest + offset) ^ 1u);
    }
    if (strcmp(scenario, "failure-trace-sdk") == 0) trace_failure = true;
    if (strcmp(scenario, "failure-allocation-sdk") == 0) allocation_failure = true;
    if (strcmp(scenario, "ownership-names") == 0) mem_write32(&host->cpu, 0x800030D0u, 0x80001000u);
    const bool fail_sdk = strncmp(scenario, "image-", 6u) == 0 || strcmp(scenario, "failure-trace-sdk") == 0 ||
        strcmp(scenario, "ownership-names") == 0 || allocation_failure;
    if (fail_sdk) {
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc));
        assert(host->error != NULL && profile->raw_hash == original_hash && !host->rel_sdk_seeded);
        goto done;
    }
    assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    assert(ww_host_rel_raw_unchanged(profile) && profile->raw_hash != original_hash);
    assert(mem_read32(&host->cpu, profile->guest + 0xBCu) == 0xAABBCCDDu);
    reach(host, 0x802B0704u);
    assert(host->cpu.gpr[4] == 0xE0u && profile->capacity == 1024u);
    if (strcmp(scenario, "ownership-resize") == 0) host->cpu.gpr[4] += 32u;
    if (strcmp(scenario, "failure-trace-shrink") == 0) trace_failure = true;
    if (strcmp(scenario, "ownership-resize") == 0 || strcmp(scenario, "failure-trace-shrink") == 0) {
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    assert(host->cpu.host_call(&host->cpu, host->cpu.pc));
    assert(host->error == NULL && host->cpu.gpr[3] == 0xE0u && profile->capacity == 1024u);
    if (strcmp(scenario, "sdk-return-reentry") == 0) {
        at(host, profile, 0x80240D4Cu, 0x80240D4Cu);
        host->cpu.gpr[3] = 1u;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    reach(host, 0x802B07ECu);
    assert(host->cpu.lr == 0x80241058u);
    assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->cpu.gpr[3] == 0xE0u);
    reach(host, 0xC1F800D4u);
    assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    assert(host->rel_pending_init.phase == WW_REL_INIT_NONE);
    if (strcmp(scenario, "query-reentry") == 0) {
        at(host, profile, 0x802B07ECu, 0x80240CC4u);
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    reach(host, 0xC0241178u);
    if (strcmp(scenario, "sequence-skip-completion") == 0) {
        host->cpu.pc = host->cpu.lr = 0x80240EE8u;
        host->cpu.gpr[1] += 16u;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    reach(host, 0xC1F800ECu);
    if (strcmp(scenario, "prolog-reentry") == 0) {
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
        at(host, profile, 0xC1F800D4u, 0x80240EE8u);
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto done;
    }
    reach(host, 0x80240EE8u);
    if (strncmp(scenario, "sequence-return-", 16u) == 0) {
        const char* field = scenario + 16u;
        if (strcmp(field, "lr") == 0) host->cpu.lr += 4u;
        else if (strcmp(field, "exception") == 0) host->cpu.exception = PPC_EXC_PROGRAM;
        else host->cpu.gpr[strtoul(field + 1u, NULL, 10)] += 4u;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        assert(!profile->initialization_completed && host->rel_pending_init.phase == WW_REL_INIT_NONE);
        goto done;
    }
    if (strcmp(scenario, "failure-trace-return") == 0) trace_failure = true;
    if (strncmp(scenario, "pending-", 8u) == 0) {
        host->rel_pending_init.phase = WW_REL_INIT_ACTIVE;
        host->rel_pending_init.module_id = (u32)strtoul(scenario + 8u, NULL, 10);
    }
    const bool fail_return = trace_failure || strncmp(scenario, "pending-", 8u) == 0;
    assert(host->cpu.host_call(&host->cpu, host->cpu.pc) == fail_return);
    if (fail_return) {
        assert(host->error != NULL && !profile->initialization_completed);
        goto done;
    }
    assert(profile->initialization_completed && profile->certificate == NULL);
    assert(host->rel_pending_init.phase == WW_REL_INIT_NONE && !host->rel_sdk_seeded);
    if (strcmp(scenario, "duplicate") == 0) assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    reach(host, 0x80001004u);
    if (strcmp(scenario, "return-reentry") == 0) {
        assert(ww_host_rel_lifecycle_boundary(host, host->cpu.pc) == NULL);
        at(host, profile, 0x80240EE8u, 0x80240EE8u);
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
    } else if (strcmp(scenario, "ship") == 0 || strncmp(scenario, "failure-trace-ship-", 19u) == 0) {
        assert(ww_host_materialize_rel(host, 0x80002200u, "d_a_ship") == NULL);
        host->cpu.lr = 0x80001004u;
        assert(ww_host_link_rel(host, 0x80002200u) == NULL);
        WwRelImage* candidate = ww_host_rel_owned(host, 0x80002200u);
        mem_write32(&host->cpu, candidate->owner + 0x14u, 0x807FF620u);
        at(host, candidate, 0x80305424u, 0x80240D0Cu);
        host->cpu.gpr[4] = 0x807FF620u;
        if (strcmp(scenario, "failure-trace-ship-prepare") == 0) {
            trace_failure = true;
            assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
            assert(!host->rel_sdk_seeded && profile->sdk_phase == WW_REL_SDK_NONE);
            assert(candidate->sdk_phase == WW_REL_SDK_NONE && ww_host_rel_raw_unchanged(candidate));
            goto done;
        }
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
        reach(host, 0x80240D0Cu);
        if (strcmp(scenario, "failure-trace-ship-finish") == 0) {
            trace_failure = true;
            const u64 hash = candidate->raw_hash;
            assert(ww_host_rel_sdk_finish(host, candidate) != NULL);
            assert(candidate->sdk_phase == WW_REL_SDK_PENDING && candidate->raw_hash == hash);
            assert(profile->sdk_phase == WW_REL_SDK_RESIDENT && host->rel_sdk_tail == profile->guest);
            goto done;
        }
        assert(ww_host_rel_sdk_finish(host, candidate) == NULL);
        assert(ww_host_rel_sdk_integrity(host) && candidate->sdk_phase == WW_REL_SDK_CERTIFIED);
        assert(profile->sdk_phase == WW_REL_SDK_RESIDENT && profile->certificate == NULL);
        at(host, candidate, host->ship_lifecycle.prolog_address, 0x80240EE8u);
        reach(host, 0x80240EE8u);
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
        assert(candidate->initialization_completed && host->rel_pending_init.phase == WW_REL_INIT_RETURN_READY);
        assert(mem_read32(&host->cpu, 0xC1B224D0u) == 2u);
        reach(host, 0x80001004u);
    }
done:
    allocation_failure = false;
    trace_failure = false;
    ww_host_shutdown(host);
    free(host);
    return 0;
}
