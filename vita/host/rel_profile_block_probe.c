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
    if (trace_failure && (strstr(format, "[rel-profile") != NULL || strstr(format, "[rel-sdk") != NULL ||
        strstr(format, "[rel-title-block]") != NULL))
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

static bool title_mutation(const char* field, const char* value) {
    return field != NULL && strcmp(field, value) == 0;
}

static void title_probe(WwHost* host, WwRelImage* profile, WwRelImage* ship,
                        WwRelImage* image, const char* scenario) {
    const char* field = strncmp(scenario, "title-bad-", 10u) == 0 ? scenario + 10u : NULL;
    const u64 hash = image->raw_hash, ship_hash = ship->raw_hash;
    const u32 tail = host->rel_sdk_tail, capacity = image->capacity;
    BlueWakeRelInitContractV1 copy = *image->certificate;
    at(host, image, 0x80240C9Cu, 0x80001004u);
    reach(host, 0x802B07ECu);
    if (strcmp(scenario, "title-adjacent-ordinary") == 0) {
        const u32 adjacent = image->guest + image->capacity;
        const u32 owner = 0x80002500u;
        const u8 bytes[32] = {0x12u, 0x34u, 0x56u, 0x78u};
        assert(ppc_guest_alias_add(adjacent, sizeof(bytes), bytes));
        u8* storage = NULL;
        assert(ppc_guest_alias_get_storage(adjacent, sizeof(bytes), &storage));
        host->cpu.gpr[3] = adjacent;
        host->cpu.gpr[31] = owner;
        mem_write32(&host->cpu, owner + 0x10u, adjacent);
        WwHost* before = malloc(sizeof(*before));
        assert(before != NULL);
        memcpy(before, host, sizeof(*before));
        assert(ww_host_rel_profile_service(host, host->cpu.pc) == WW_REL_PROFILE_MISSED);
        assert(memcmp(before, host, sizeof(*before)) == 0);
        assert(memcmp(storage, bytes, sizeof(bytes)) == 0);
        assert(mem_read32(&host->cpu, owner + 0x10u) == adjacent);
        free(before);
        assert(ppc_guest_alias_remove(adjacent, sizeof(bytes)));
        return;
    }
    if (strcmp(scenario, "title-route-pending-exception") == 0) {
        host->cpu.exception = PPC_EXC_PROGRAM;
        const CPUState before = host->cpu;
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
        assert(memcmp(&before, &host->cpu, sizeof(before)) == 0 && image->raw_hash == hash);
        assert(host->rel_title_block.phase == WW_REL_PROFILE_EXPOSED);
        return;
    }
    if (title_mutation(field, "trace-query")) trace_failure = true;
    if (title_mutation(field, "heap")) host->cpu.gpr[4] = 4u;
    if (title_mutation(field, "interior")) host->cpu.gpr[3] += 4u;
    if (title_mutation(field, "one-past")) host->cpu.gpr[3] += image->capacity;
    if (title_mutation(field, "mirror")) host->cpu.gpr[3] |= 0x40000000u;
    if (title_mutation(field, "neighbor-alias")) host->cpu.gpr[3] = ship->guest;
    if (title_mutation(field, "publication")) mem_write32(&host->cpu, image->owner + 0x10u, 0u);
    if (title_mutation(field, "owner-bss")) mem_write32(&host->cpu, image->owner + 0x14u, 0x80006000u);
    if (title_mutation(field, "duplicate-module")) {
        host->rel_images[host->rel_image_count] = *image;
        host->rel_images[host->rel_image_count++].sections = NULL;
    }
    if (title_mutation(field, "mapping")) assert(ppc_guest_alias_remove(image->guest, image->capacity));
    if (title_mutation(field, "certificate-missing")) image->certificate = NULL;
    if (title_mutation(field, "certificate-copy")) image->certificate = &copy;
    if (title_mutation(field, "certificate-reassociate")) image->certificate = ship->certificate;
    if (title_mutation(field, "raw")) mem_write8(&host->cpu, image->guest + image->bytes - 1u, 1u);
    if (title_mutation(field, "sections")) image->sections[1].size += 4u;
    if (title_mutation(field, "region")) image->guest = WW_REL_SCRATCH_LIMIT - 32u;
    if (title_mutation(field, "slack")) image->capacity = 0x150u;
    if (title_mutation(field, "header-bss") || title_mutation(field, "section-bss") ||
        title_mutation(field, "fix-overflow") || title_mutation(field, "extent")) {
        const u32 offset = title_mutation(field, "header-bss") ? 0x20u :
            title_mutation(field, "section-bss") ? 0x68u : 0x48u;
        mem_write32(&host->cpu, image->guest + offset,
            title_mutation(field, "fix-overflow") ? UINT32_MAX : image->bytes + 32u);
    }
    const bool query_bad = field != NULL && (title_mutation(field, "trace-query") || title_mutation(field, "heap") || title_mutation(field, "interior") ||
        title_mutation(field, "one-past") || title_mutation(field, "mirror") || title_mutation(field, "neighbor-alias") ||
        title_mutation(field, "publication") || title_mutation(field, "owner-bss") ||
        title_mutation(field, "duplicate-module") || title_mutation(field, "mapping") ||
        strncmp(field, "certificate-", 12u) == 0 || title_mutation(field, "raw") || title_mutation(field, "sections") ||
        title_mutation(field, "region") || title_mutation(field, "slack") || title_mutation(field, "header-bss") ||
        title_mutation(field, "section-bss") || title_mutation(field, "fix-overflow") || title_mutation(field, "extent"));
    if (query_bad) {
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto refused;
    }
    if (title_mutation(field, "prepare")) {
        assert(ww_host_rel_sdk_prepare(host, image, true) != NULL);
        goto refused;
    }
    if (title_mutation(field, "missing-query")) {
        at(host, image, 0x80305448u, 0x80240D4Cu);
        host->cpu.gpr[4] = image->guest + 0x140u;
    } else {
        if (strcmp(scenario, "title-route-edge") == 0)
            assert(ww_host_edge(host, &host->cpu, host->cpu.pc));
        else if (strcmp(scenario, "title-route-dispatch") == 0)
            assert(ww_host_dispatch(host, host->cpu.pc) == 1);
        else
            assert(host->cpu.host_call(&host->cpu, host->cpu.pc));
        assert(host->error == NULL && !host->edge_stopped && host->cpu.gpr[3] == capacity);
        if (strcmp(scenario, "title-route-duplicate") == 0) {
            at(host, image, 0x802B07ECu, 0x80240CC4u);
            assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error == NULL);
        }
        reach(host, 0x80305448u);
    }
    assert(host->cpu.gpr[4] == image->guest + 0x140u && host->cpu.gpr[4] >= GC_RAM_BASE + GC_MAIN_RAM_SIZE);
    assert(mem_read32(&host->cpu, image->owner + 0x14u) == 0u);
    mem_write32(&host->cpu, 0x800030D0u, 0x80006000u);
    if (title_mutation(field, "normal")) host->cpu.pc = 0x80305424u;
    if (title_mutation(field, "fallback")) host->cpu.lr = 0x80240E28u;
    if (title_mutation(field, "pc")) host->cpu.pc += 4u;
    if (title_mutation(field, "lr")) host->cpu.lr += 4u;
    if (title_mutation(field, "r3")) host->cpu.gpr[3] = 0x80006000u;
    if (title_mutation(field, "r4")) host->cpu.gpr[4] += 4u;
    if (title_mutation(field, "inline-interior")) host->cpu.gpr[4] -= 32u;
    if (title_mutation(field, "inline-one-past")) host->cpu.gpr[4] = image->guest + image->capacity;
    if (title_mutation(field, "inline-neighbor")) host->cpu.gpr[4] = ship->guest;
    if (title_mutation(field, "inline-mirror")) host->cpu.gpr[4] |= 0x40000000u;
    if (title_mutation(field, "trace-refusal")) { trace_failure = true; host->cpu.gpr[4] += 4u; }
    if (title_mutation(field, "r31")) host->cpu.gpr[31] += 32u;
    if (title_mutation(field, "exception")) host->cpu.exception = PPC_EXC_PROGRAM;
    if (title_mutation(field, "name-unbounded") || title_mutation(field, "name-overflow"))
        mem_write32(&host->cpu, 0x800030D0u, UINT32_MAX);
    if (title_mutation(field, "skip-profile")) profile->sdk_phase = WW_REL_SDK_NONE;
    if (title_mutation(field, "skip-ship")) host->rel_sdk_tail = profile->guest;
    if (title_mutation(field, "ship-incomplete")) ship->initialization_completed = false;
    if (title_mutation(field, "trace-prepare")) trace_failure = true;
    const bool entry_bad = field != NULL && (title_mutation(field, "normal") || title_mutation(field, "fallback") ||
        title_mutation(field, "missing-query") || title_mutation(field, "pc") || title_mutation(field, "lr") ||
        title_mutation(field, "r3") || title_mutation(field, "r4") || title_mutation(field, "r31") ||
        strncmp(field, "inline-", 7u) == 0 || title_mutation(field, "trace-refusal") ||
        title_mutation(field, "exception") || strncmp(field, "name-", 5u) == 0 ||
        title_mutation(field, "skip-profile") || title_mutation(field, "skip-ship") ||
        title_mutation(field, "ship-incomplete") || title_mutation(field, "trace-prepare"));
    if (entry_bad) {
        const u32 observed = title_mutation(field, "pc") ? 0x80305448u : host->cpu.pc;
        assert(ww_host_rel_sdk_observe(host, observed) != NULL);
        goto refused;
    }
    assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    if (field != NULL && (strncmp(field, "duplicate-", 10u) == 0 || title_mutation(field, "sdk-reentry") ||
        title_mutation(field, "input-form"))) {
        if (title_mutation(field, "duplicate-r4")) host->cpu.gpr[4] += 4u;
        if (title_mutation(field, "duplicate-names")) mem_write32(&host->cpu, 0x800030D0u, 0x80006004u);
        if (title_mutation(field, "duplicate-neighbor")) mem_write8(&host->cpu, ship->guest + ship->bytes - 1u, 1u);
        if (title_mutation(field, "input-form")) image->sdk_input_form = (WwRelSdkInputForm)99;
        if (title_mutation(field, "sdk-reentry")) {
            host->cpu.pc = 0x80009000u;
            assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
            host->cpu.pc = 0x80305448u;
        }
        assert(ww_host_rel_sdk_observe(host, 0x80305448u) != NULL);
        goto refused;
    }
    if (strcmp(scenario, "title-route-duplicate") == 0)
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    if (title_mutation(field, "finish")) {
        assert(ww_host_rel_sdk_finish(host, image) != NULL);
        goto refused;
    }
    reach(host, 0x80240D4Cu);
    assert(mem_read32(&host->cpu, image->guest + 0x48u) == image->guest + 0x13Cu);
    assert(mem_read32(&host->cpu, image->guest + 0x2Cu) == 0u);
    if (title_mutation(field, "result")) host->cpu.gpr[3] = 0u;
    if (title_mutation(field, "names")) mem_write32(&host->cpu, 0x800030D0u, 0x80006004u);
    if (title_mutation(field, "pre-trim") || title_mutation(field, "nonfixed"))
        mem_write32(&host->cpu, image->guest + 0x2Cu, 16u);
    if (title_mutation(field, "header")) mem_write32(&host->cpu, image->guest + 0x14u, 0u);
    if (title_mutation(field, "relocation")) mem_write8(&host->cpu, image->guest + 0xAEu, 0u);
    if (title_mutation(field, "untouched")) mem_write8(&host->cpu, image->guest + image->bytes - 1u, 1u);
    if (title_mutation(field, "head")) mem_write32(&host->cpu, 0x800030C8u, image->guest);
    if (title_mutation(field, "tail")) mem_write32(&host->cpu, 0x800030CCu, ship->guest);
    if (title_mutation(field, "previous")) mem_write32(&host->cpu, image->guest + 8u, profile->guest);
    if (title_mutation(field, "next")) mem_write32(&host->cpu, ship->guest + 4u, 0u);
    if (title_mutation(field, "trace-finish")) trace_failure = true;
    if (title_mutation(field, "allocation")) allocation_failure = true;
    const bool return_bad = field != NULL && (title_mutation(field, "result") || title_mutation(field, "names") ||
        title_mutation(field, "pre-trim") || title_mutation(field, "nonfixed") || title_mutation(field, "header") ||
        title_mutation(field, "relocation") || title_mutation(field, "untouched") || title_mutation(field, "head") ||
        title_mutation(field, "tail") || title_mutation(field, "previous") || title_mutation(field, "next") ||
        title_mutation(field, "trace-finish") || title_mutation(field, "allocation"));
    if (return_bad) {
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto refused;
    }
    if (strcmp(scenario, "title-route-edge") == 0)
        assert(!ww_host_edge(host, &host->cpu, host->cpu.pc));
    else if (strcmp(scenario, "title-route-dispatch") == 0) {
        host->cpu.cycle_budget = 0;
        host->cpu.downcount = 0;
        assert(ww_host_dispatch(host, host->cpu.pc) == 1);
        assert(host->cpu.pc == 0x80240D4Cu);
        host->cpu.cycle_budget = 1000;
    } else
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    assert(host->error == NULL && !host->edge_stopped && image->sdk_phase == WW_REL_SDK_CERTIFIED && host->rel_sdk_tail == image->guest);
    assert(ww_host_rel_sdk_integrity(host) && image->sdk_fixed && !ship->sdk_fixed);
    assert(ship->sdk_phase == WW_REL_SDK_CERTIFIED && profile->sdk_phase == WW_REL_SDK_RESIDENT);
    assert(profile->certificate == NULL && ship->initialization_completed && !image->initialization_completed);
    if (strcmp(scenario, "title-route-duplicate") == 0)
        assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    reach(host, 0x802B0704u);
    if (title_mutation(field, "prolog") || title_mutation(field, "skip-shrink"))
        at(host, image, 0xC1E000DCu, 0x80240EE8u);
    if (title_mutation(field, "resize")) host->cpu.gpr[4] += 32u;
    if (title_mutation(field, "trace-shrink")) trace_failure = true;
    if (title_mutation(field, "query-reentry")) at(host, image, 0x802B07ECu, 0x80240CC4u);
    if (title_mutation(field, "prolog") || title_mutation(field, "skip-shrink") ||
        title_mutation(field, "resize") || title_mutation(field, "query-reentry") || title_mutation(field, "trace-shrink")) {
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto certified_refusal;
    }
    assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error == NULL && host->cpu.gpr[3] == 0x140u);
    assert(image->capacity == capacity && ww_host_rel_raw_unchanged(image));
    if (title_mutation(field, "return-reentry")) {
        at(host, image, 0x80240D4Cu, 0x80240D4Cu);
        host->cpu.gpr[3] = 1u;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto certified_refusal;
    }
    reach(host, 0x802B07ECu);
    if (title_mutation(field, "trace-accounting")) {
        trace_failure = true;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto certified_refusal;
    }
    if (title_mutation(field, "skip-accounting")) {
        at(host, image, 0xC1E000DCu, 0x80240EE8u);
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto certified_refusal;
    }
    assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->cpu.gpr[3] == 0x140u);
    reach(host, 0xC1E000DCu);
    assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    if (title_mutation(field, "constructor") || title_mutation(field, "caller-return")) {
        host->cpu.pc = host->cpu.lr = 0x80240EE8u;
        assert(host->cpu.host_call(&host->cpu, host->cpu.pc) && host->error != NULL);
        goto certified_refusal;
    }
    reach(host, 0x80240EE8u);
    assert(!host->cpu.host_call(&host->cpu, host->cpu.pc));
    assert(image->initialization_completed && ww_host_rel_sdk_integrity(host));
    assert(field == NULL);
    return;
refused:
    assert(image->raw_hash == hash && ship->raw_hash == ship_hash &&
        host->rel_sdk_tail == (title_mutation(field, "skip-ship") ? profile->guest : tail));
    assert(image->sdk_phase != WW_REL_SDK_CERTIFIED);
certified_refusal:
    assert(!image->initialization_completed && profile->sdk_phase ==
        (title_mutation(field, "skip-profile") ? WW_REL_SDK_NONE : WW_REL_SDK_RESIDENT));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRelModule descriptors[] = {fixture_rel,
        {.module_id=1u,.section_count=5u,.section_info_offset=0x4Cu,.file_size=1024u,
         .sections=resident_sections,.num_sections=3u},
        {.module_id=400u,.section_count=8u,.section_info_offset=0x4Cu,.file_size=sizeof(block_title_raw),
         .sections=block_title_sections,.num_sections=2u}};
    StaticRecompRange chunks[] = {fixture_chunks[0],fixture_chunks[1],fixture_chunks[2],
                                 {0xC1E000DCu,0xC1E0013Cu},{0xC1F800D4u,0xC1F80100u}};
    u64 hashes[] = {fixture_hashes[0],fixture_hashes[1],fixture_hashes[2],block_title_hash,profile_code_hash};
    StaticRecompModuleDesc module = {.code_ranges=profile_ranges,
        .num_code_ranges=sizeof(profile_ranges)/sizeof(profile_ranges[0]),.chunk_ranges=chunks,
        .chunk_hashes=hashes,.num_chunk_ranges=5u,.rel_modules=descriptors,.num_rel_modules=3u,
        .dispatch=translated_dispatch};
    BlueWakeRelData data[] = {fixture_data[0],fixture_data[1],fixture_data[2],
        {1u,2u,0xC1F80170u,4u,resident_ctors},{1u,4u,0xC1F80178u,16u,resident_data},block_title_canonical[1]};
    host->module = &module;
    host->rel_data = data;
    host->rel_data_count = 6u;
    BlueWakeRelInitContractV1 title_certificate = {.module_id=400u,.rel_bytes=block_title_raw,
        .rel_size=sizeof(block_title_raw),.canonical_sections=block_title_canonical,.section_count=2u,
        .ctor_table_section=2u,.ctor_return_pc=0xC1E000F4u,.lifecycle=&block_title_lifecycle,
        .build_identity=s_ship_contract.build_identity};
    BlueWakeRelInitContractSetV1 set = {1u,s_ship_contract.dol_size,s_ship_contract.dol_bytes,
        &title_certificate,1u,s_ship_contract.build_identity};
    const char* registration = ww_host_register_rel_contracts(host, &set, "synthetic.dol");
    if (registration != NULL) fprintf(stderr, "registration: %s\n", registration);
    assert(registration == NULL);
    const bool off = strcmp(scenario, "policy-off") == 0;
    const bool title = strcmp(scenario, "policy-title") == 0;
    const bool ship = strcmp(scenario, "policy-ship") == 0;
    if (!off && !title)
        assert(ww_host_enable_ship_contract(host, &s_ship_contract, "synthetic.dol") == NULL);
    host->rel_title_admission = !off && !ship;
    if (strncmp(scenario, "title-", 6u) == 0)
        mem_write32(&host->cpu, 0x800030D0u, 0x80006000u);
    for (u32 index = 0u; index < host->rel_data_count; ++index)
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
    } else if (strcmp(scenario, "ship") == 0 || strncmp(scenario, "failure-trace-ship-", 19u) == 0 ||
               strncmp(scenario, "title-", 6u) == 0) {
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
        if (strncmp(scenario, "title-", 6u) == 0) {
            assert(ww_host_rel_lifecycle_boundary(host, host->cpu.pc) == NULL);
            assert(ww_host_materialize_rel(host, 0x80002300u, "d_a_title") == NULL);
            host->cpu.lr = 0x80001004u;
            WwRelImage* title_image = ww_host_rel_owned(host, 0x80002300u);
            if (strncmp(scenario, "title-expose-", 13u) == 0) {
                const char* field = scenario + 13u;
                u8 raw[sizeof(block_title_raw)];
                memcpy(raw, block_title_raw, sizeof(raw));
                title_certificate.rel_bytes = raw;
                if (title_mutation(field, "trace")) trace_failure = true;
                u32 offset = 0x48u;
                u32 value = title_mutation(field, "fix-overflow") ? UINT32_MAX :
                    title_mutation(field, "fix-small") ? 0x4Bu :
                    title_mutation(field, "extent-at-bytes") ? sizeof(raw) :
                    title_mutation(field, "trace") ? 0x13Cu : 0x100u;
                if (title_mutation(field, "bss-alignment")) { offset = 0x44u; value = 4096u; }
                if (title_mutation(field, "section-tail")) {
                    offset = 0x5Cu; value = 0x1F0u;
                    title_image->sections[2].flags = value;
                }
                write_be32(raw + offset, value);
                mem_write32(&host->cpu, title_image->guest + offset, value);
                title_image->raw_hash = ww_host_rel_raw_hash(raw, sizeof(raw));
                assert(ww_host_link_rel(host, title_image->owner) != NULL);
                assert(title_image->linked && host->rel_title_block.phase == WW_REL_PROFILE_FAILED);
                assert(title_image->sdk_phase == WW_REL_SDK_NONE && !title_image->initialization_completed);
                assert(host->rel_sdk_tail == candidate->guest && candidate->sdk_next == 0u);
                title_certificate.rel_bytes = block_title_raw;
                goto done;
            }
            assert(ww_host_link_rel(host, 0x80002300u) == NULL);
            assert(ww_host_link_rel(host, 0x80002300u) == NULL);
            title_probe(host, profile, candidate, title_image, scenario);
        }
    }
done:
    allocation_failure = false;
    trace_failure = false;
    ww_host_shutdown(host);
    free(host);
    return 0;
}
