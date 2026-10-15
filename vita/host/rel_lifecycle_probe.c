#include "probe_support.h"
#include "dispatch_loop.h"
#include "external_vector.h"
#include <stdarg.h>
#include <stdlib.h>
#include "ship_fixture.inc"
#include "ship_contract.inc"
#define DOLRECOMP_CPU_HEADER "core/cpu.h"
#define DOLRECOMP_C_LOOP_CYCLE_BUDGET (ctx->cycle_budget)
#include "lifecycle_translated.c"
#include "rel_lifecycle_fixture.inc"
#include "ikada_fixture.inc"

static const char* scenario;
static WwRelImage* selected;
static WwRelImage* neighbor;
static bool injected;
static bool stop_before_entry;
static u32 handoffs;
static u32 restorations;
static u32 entry_yields;
static u32 return_yields;
static bool trace_failure;
static u32 requested_owner;
static BlueWakeRelInitContractV1 copied_certificate;
static bool profile_call;
static bool ikada_call;
static u32 sdk_allocations;
static bool sdk_allocation_armed;

void* __real_calloc(size_t count, size_t size);
void* __wrap_calloc(size_t count, size_t size) {
    if (ikada_call && strcmp(scenario, "materialize-allocation") == 0 && count == 21u && size == sizeof(WwRelSection))
        return NULL;
    return __real_calloc(count, size);
}

void* __real_malloc(size_t size);
void* __wrap_malloc(size_t size) {
    if (sdk_allocation_armed && selected != NULL && size == selected->bytes && sdk_allocations++ == 1u)
        return NULL;
    return __real_malloc(size);
}

static u32 ctor_table(void) {
    return selected->sections[selected->certificate->ctor_table_section].linked_start;
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (ikada_call && strcmp(scenario, "materialize-trace") == 0 && strstr(format, "[rel-materialized]") != NULL)
        return -1;
    if (trace_failure && (strstr(format, "[rel-prolog]") != NULL ||
        (ikada_call && (strstr(format, "[rel-sdk]") != NULL || strstr(format, "[rel-admission]") != NULL))))
        return -1;
    va_list args;
    va_start(args, format);
    const int result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

static bool is(const char* value) {
    return strcmp(scenario, value) == 0;
}

static void mutate_register(CPUState* cpu, const char* field) {
    if (strcmp(field, "lr") == 0) cpu->lr += 4u;
    else if (strcmp(field, "pc") == 0) cpu->pc += 4u;
    else if (strcmp(field, "r1") == 0) cpu->gpr[1] += 16u;
    else if (strcmp(field, "r2") == 0) cpu->gpr[2] += 4u;
    else if (strcmp(field, "r13") == 0) cpu->gpr[13] += 4u;
    else if (strcmp(field, "r31") == 0) cpu->gpr[31] += 32u;
    else { assert(strcmp(field, "r3") == 0); cpu->gpr[3] += 4u; }
}

static void tamper(WwHost* host, const char* field) {
    CPUState* cpu = &host->cpu;
    if (strcmp(field, "owner") == 0) mem_write32(cpu, selected->owner + 0x10u, 0u);
    else if (strcmp(field, "neighbor") == 0) mem_write8(cpu, neighbor->guest + neighbor->bytes - 1u, 1u);
    else if (strcmp(field, "certificate") == 0) selected->certificate = neighbor->certificate;
    else if (strcmp(field, "code") == 0) mem_write8(cpu, selected->guest + (selected->sections[1].flags & ~1u) + 4u, 0u);
    else { assert(strcmp(field, "header") == 0); mem_write32(cpu, selected->guest + 0x14u, 1u); }
}

static bool interrupt_edge(WwHost* host, CPUState* cpu, u32 address) {
    if (host->rel_pending_init.phase == WW_REL_INIT_RETURN_READY && restorations < handoffs) {
        assert(host->rel_pending_init.rfi_stage == WW_REL_RFI_HANDLER);
        assert((cpu->msr & (PPC_MSR_EE | PPC_MSR_FP)) == (PPC_MSR_EE | PPC_MSR_FP));
        assert((cpu->msr & PPC_MSR_POW) == 0u);
        ++restorations;
    }
    const u32 count = is("interrupt-sequential") ? 2u : 1u;
    if (handoffs == count)
        return false;
    const bool external = !is("interrupt-decrementer") && handoffs == 0u;
    const u32 vector = external ? WW05_EXTERNAL_VECTOR : WW05_DECREMENTER_VECTOR;
    memcpy(cpu->ram + vector, external ? ww05_external_vector_reference : ww05_decrementer_vector_reference,
           WW05_EXTERNAL_VECTOR_BYTES);
    mem_write32(cpu, 0x800000C0u, 0x80006000u);
    mem_write32(cpu, 0x80003010u, 0x80007000u);
    if (external) {
        cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_SI, 4u);
        cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010301u, 4u);
    } else {
        host->decrementer_pending = true;
    }
    assert(ww_host_edge(host, cpu, address));
    assert(host->error == NULL && cpu->pc == vector);
    assert(host->rel_pending_init.phase == WW_REL_INIT_RETURN_SUSPENDED);
    assert(host->rel_pending_init.rfi_stage == WW_REL_RFI_VECTOR);
    assert(!ww_host_rel_return_qualified(host, address));
    ++handoffs;
    if (external)
        cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
    if (is("rfi-no-stage")) {
        cpu->gpr[4] = 0x80006000u;
        mem_write32(cpu, 0x80006198u, address);
        mem_write32(cpu, 0x8000619Cu, host->rel_pending_init.caller_msr);
        cpu->pc = 0x80007000u;
    }
    if (is("vector-tamper")) cpu->ram[vector + 16u] ^= 1u;
    if (is("vector-wrong-class")) {
        memcpy(cpu->ram + WW05_DECREMENTER_VECTOR, ww05_decrementer_vector_reference, WW05_EXTERNAL_VECTOR_BYTES);
        cpu->pc = WW05_DECREMENTER_VECTOR;
    }
    if (is("nested-vector")) cpu->exception = PPC_EXC_PROGRAM;
    return true;
}

static bool probe_edge(void* user, CPUState* cpu, u32 address) {
    WwHost* host = user;
    selected = ww_host_rel_owned(host, requested_owner);
    const u32 pc = ww_host_rel_address(host, address) & ~0x40000000u;
    const u32 entry = selected == NULL || selected->certificate == NULL ? 0u : selected->certificate->lifecycle->prolog_address & ~0x40000000u;
    if (profile_call)
        return ww_host_edge(host, cpu, address);
    if (ikada_call && selected != NULL && pc == 0x80305424u) {
        if (is("sdk-prepared-trace")) trace_failure = true;
        if (is("sdk-wrong-argument")) cpu->gpr[4] += 8u;
        if (is("sdk-null-bss")) cpu->gpr[4] = 0u;
        if (is("sdk-misaligned-bss")) cpu->gpr[4] += 4u;
        if (is("sdk-out-of-range-bss")) cpu->gpr[4] = 0x817FFFF8u;
        if (is("sdk-alias-bss")) cpu->gpr[4] = 0x81820100u;
        if (is("sdk-null-bss") || is("sdk-misaligned-bss") || is("sdk-out-of-range-bss") || is("sdk-alias-bss"))
            mem_write32(cpu, selected->owner + 0x14u, cpu->gpr[4]);
        if (is("sdk-missing-neighbor")) mem_write32(cpu, 0x800030CCu, 0u);
        if (is("sdk-owner-mismatch")) mem_write32(cpu, selected->owner + 0x10u, 0u);
    }
    if (ikada_call && selected != NULL && pc == entry && !injected) {
        if (is("sdk-allocation")) sdk_allocation_armed = true;
        if (is("sdk-verified-trace")) trace_failure = true;
        if (is("sdk-frozen-name")) mem_write32(cpu, 0x800030D0u, 0x80005000u);
        if (is("sdk-frozen-bss")) mem_write32(cpu, selected->owner + 0x14u, 0x807FE008u);
        if (is("sdk-head")) mem_write32(cpu, 0x800030C8u, selected->guest);
        if (is("sdk-tail")) mem_write32(cpu, 0x800030CCu, neighbor->guest);
        if (is("sdk-previous")) mem_write32(cpu, selected->guest + 8u, 0u);
        if (is("sdk-next")) mem_write32(cpu, selected->guest + 4u, neighbor->guest);
        if (is("sdk-neighbor-write")) mem_write8(cpu, neighbor->guest + neighbor->bytes - 1u, 1u);
        if (is("sdk-untouched-byte")) mem_write8(cpu, selected->guest + selected->bytes - 1u, 1u);
        if (is("sdk-header")) mem_write32(cpu, selected->guest + 0x18u, 4u);
        if (is("sdk-relocation")) mem_write32(cpu, selected->guest + 0x174u, 0u);
    }
    if (selected != NULL && pc == entry && stop_before_entry)
        return true;
    if (selected != NULL && pc == entry && !injected) {
        if (is("title-disabled") || is("title-catalog-id") || is("title-copy")) {
            host->rel_test_admission = false;
            if (is("title-copy")) {
                copied_certificate = *selected->certificate;
                selected->certificate = &copied_certificate;
            }
            injected = true;
        }
        if (strncmp(scenario, "entry-", 6u) == 0 && !is("entry-after-progress") &&
            !is("entry-after-walker") && !is("entry-trace") && !is("entry-certified-r3")) {
            mutate_register(cpu, scenario + 6u);
            injected = true;
        }
        if (is("entry-trace")) { trace_failure = true; injected = true; }
        if (is("ctor-before-entry")) mem_write32(cpu, ctor_table(), selected->certificate->lifecycle->prolog_address);
        if (ikada_call && is("copied-certificate")) {
            copied_certificate = *selected->certificate;
            selected->certificate = &copied_certificate;
            injected = true;
        }
#if WW05_ENABLE_IKADA_CONTRACT
        if (ikada_call && is("policy-disabled")) host->rel_ikada_admission = false;
#endif
    }
    if (selected != NULL && pc == entry && host->rel_pending_init.phase == WW_REL_INIT_ACTIVE) {
        if (is("observations") && entry_yields < 2u) {
            const bool stopped = ww_host_edge(host, cpu, address);
            cpu->cycle_budget = 0;
            cpu->downcount = 0;
            ++entry_yields;
            return stopped;
        }
        if (!injected && strncmp(scenario, "duplicate-", 10u) == 0 && !is("duplicate-owner")) {
            mutate_register(cpu, scenario + 10u);
            injected = true;
        }
    }
    if (selected != NULL && host->rel_pending_init.image == selected && pc == 0x80241178u) {
        if (!injected && is("entry-after-progress")) {
            assert(host->rel_pending_init.phase == WW_REL_INIT_ACTIVE);
            assert(!ww_host_edge(host, cpu, address));
            cpu->pc = selected->certificate->lifecycle->prolog_address;
            cpu->gpr[1] = host->rel_pending_init.r1;
            cpu->gpr[3] = selected->guest;
            cpu->lr = 0x80240EE8u;
            injected = true;
            return ww_host_edge(host, cpu, cpu->pc);
        }
        if (!injected && is("walker-table")) cpu->gpr[3] += 4u;
        if (!injected && is("walker-successor")) cpu->lr += 4u;
        if (!injected && is("ctor-terminator")) mem_write32(cpu, ctor_table(), selected->certificate->lifecycle->prolog_address);
        if (!injected && is("return-without-walker")) {
            cpu->pc = cpu->lr = 0x80240EE8u;
            cpu->gpr[1] = host->rel_pending_init.r1;
            injected = true;
            return ww_host_edge(host, cpu, cpu->pc);
        }
        if (!injected && strncmp(scenario, "active-tamper-", 14u) == 0)
            tamper(host, scenario + 14u);
        if (is("completion-trace")) trace_failure = true;
        injected = true;
    }
    if (selected != NULL && host->rel_constructor_owner == selected->owner && pc != 0x80241178u) {
        if (is("return-outstanding-walker")) {
            cpu->pc = cpu->lr = 0x80240EE8u;
            cpu->gpr[1] = host->rel_pending_init.r1;
            cpu->gpr[31] = selected->owner;
            return ww_host_edge(host, cpu, cpu->pc);
        }
        if (is("walker-reentry")) {
            cpu->pc = 0x80241178u;
            cpu->gpr[3] = ctor_table();
            cpu->lr = selected->certificate->ctor_return_pc;
            return ww_host_edge(host, cpu, cpu->pc);
        }
        if (is("ctor-during-walk")) mem_write32(cpu, ctor_table(), selected->certificate->lifecycle->prolog_address);
    }
    if (selected != NULL && pc == (selected->certificate->ctor_return_pc & ~0x40000000u) && is("entry-after-walker")) {
        assert(!ww_host_edge(host, cpu, address));
        assert(host->rel_pending_init.constructor_completed && host->rel_constructor_owner == 0u);
        cpu->pc = selected->certificate->lifecycle->prolog_address;
        cpu->lr = 0x80240EE8u;
        cpu->gpr[1] = host->rel_pending_init.r1;
        cpu->gpr[3] = selected->guest;
        return ww_host_edge(host, cpu, cpu->pc);
    }
    if (selected != NULL && host->rel_pending_init.image == selected && pc == 0x80240EE8u) {
        if (strncmp(scenario, "return-", 7u) == 0 && !is("return-without-walker") && !is("return-trace"))
            mutate_register(cpu, scenario + 7u);
        if (is("return-trace")) trace_failure = true;
        const bool stopped = ww_host_edge(host, cpu, address);
        if (stopped)
            return true;
        if (is("observations") && return_yields < 2u) {
            cpu->cycle_budget = 0;
            ++return_yields;
        }
        if (strncmp(scenario, "ready-tamper-", 13u) == 0) {
            tamper(host, scenario + 13u);
            return ww_host_edge(host, cpu, address);
        }
        if (strncmp(scenario, "interrupt-", 10u) == 0 || strncmp(scenario, "rfi-", 4u) == 0 ||
            strncmp(scenario, "nested-", 7u) == 0 || strncmp(scenario, "vector-", 7u) == 0 ||
            strncmp(scenario, "suspended-", 10u) == 0)
            return interrupt_edge(host, cpu, address);
        return false;
    }
    if (selected != NULL && pc == 0x80007000u && host->rel_pending_init.phase == WW_REL_INIT_RETURN_SUSPENDED) {
        if (!is("rfi-no-stage")) {
            assert(host->rel_pending_init.rfi_stage == WW_REL_RFI_HANDLER);
            assert(!ww_host_rel_return_qualified(host, 0x80240EE8u));
        }
        if (is("rfi-misaligned")) mem_write32(cpu, 0x80006198u, 0x80240EE9u);
        if (is("rfi-diverted")) mem_write32(cpu, 0x80006198u, 0x80240EECu);
        if (is("rfi-prolog")) mem_write32(cpu, 0x80006198u, selected->certificate->lifecycle->prolog_address);
        if (is("rfi-fp")) mem_write32(cpu, 0x8000619Cu, host->rel_pending_init.caller_msr & ~PPC_MSR_FP);
        if (is("rfi-ee")) mem_write32(cpu, 0x8000619Cu, host->rel_pending_init.caller_msr & ~PPC_MSR_EE);
        if (is("rfi-text-tamper")) cpu->ram[0x7018u] ^= 1u;
        if (is("suspended-context")) mem_write32(cpu, 0x800000C0u, 0x80006200u);
        if (is("suspended-handler")) mem_write32(cpu, 0x80003010u, 0x80007004u);
        if (strncmp(scenario, "suspended-tamper-", 17u) == 0) tamper(host, scenario + 17u);
        if (strncmp(scenario, "rfi-register-", 13u) == 0) mutate_register(cpu, scenario + 13u);
        if (is("nested-handler")) cpu->exception = PPC_EXC_PROGRAM;
        if (is("nested-interrupt")) {
            cpu->msr |= PPC_MSR_EE;
            host->decrementer_pending = true;
        }
        if (is("suspended-direct") || is("suspended-prolog")) {
            cpu->pc = is("suspended-direct") ? 0x80240EE8u : selected->certificate->lifecycle->prolog_address;
            return ww_host_edge(host, cpu, cpu->pc);
        }
        if (is("rfi-outside-dol")) {
            cpu->pc = 0x8000B000u;
            return true;
        }
    }
    return ww_host_edge(host, cpu, address);
}

static int translated_chunk(CPUState* cpu, u32 address) {
    for (u32 at = 0u; at < sizeof(lifecycle_chunks) / sizeof(lifecycle_chunks[0]); ++at)
        if ((address & ~0x40000000u) >= lifecycle_chunks[at].start &&
            (address & ~0x40000000u) < lifecycle_chunks[at].end) {
            lifecycle_chunks[at].run(cpu);
            return 1;
        }
    return 0;
}

static int translated_dispatch(CPUState* cpu, u32 address) {
    return bluewake_chassis_dispatch_loop(cpu, address, translated_chunk, probe_edge, cpu->external_user_data);
}

static void run_call(WwHost* host, u32 owner, const char* name) {
    requested_owner = owner;
    CPUState* cpu = &host->cpu;
    mem_write32(cpu, owner + 0x1Cu, 0x80003000u);
    strcpy((char*)cpu->ram + 0x3000u, name);
    cpu->gpr[3] = owner;
    cpu->gpr[31] = 0x12345678u;
    cpu->gpr[1] = 0x80004000u;
    cpu->gpr[2] = 0x80300000u;
    cpu->gpr[13] = 0x80400000u;
    cpu->lr = 0x80001004u;
    cpu->pc = 0x80240EA0u;
    if (ww_host_rel_profile_enabled(host) && !(is("title-catalog-id") && strcmp(name, "d_a_title") == 0))
        cpu->pc = strcmp(name, "d_a_title") == 0 ? 0x80240B40u : 0x80240B80u;
    cpu->msr = PPC_MSR_FP | PPC_MSR_EE | PPC_MSR_POW;
    for (u32 turn = 0u; turn < 128u && cpu->pc != 0x80001004u && host->error == NULL; ++turn) {
        ww_host_end_dispatch(host);
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        cpu->cycle_budget = 80;
        cpu->downcount = 0;
        if (probe_edge(host, cpu, cpu->pc)) {
            if (stop_before_entry && selected != NULL &&
                (ww_host_rel_address(host, cpu->pc) & ~0x40000000u) ==
                    (selected->certificate->lifecycle->prolog_address & ~0x40000000u))
                break;
            continue;
        }
        const int result = ww_host_dispatch(host, cpu->pc);
        assert(result != 0 || host->error != NULL);
        selected = ww_host_rel_owned(host, owner);
        if (stop_before_entry && selected != NULL &&
            (ww_host_rel_address(host, cpu->pc) & ~0x40000000u) ==
                (selected->certificate->lifecycle->prolog_address & ~0x40000000u))
            break;
    }
}

#if WW05_ENABLE_IKADA_CONTRACT
static void run_ikada(WwHost* host, const char* requested, WwRelImage* profile) {
    scenario = "success";
    run_call(host, 0x80002300u, "d_a_title");
    if (host->error != NULL) fprintf(stderr, "title prefix: %s\n", host->error);
    assert(host->error == NULL && selected != NULL && selected->initialization_completed);
    assert(selected->guest == 0x818325E0u && selected->raw_hash == lifecycle_expected_title_hash);
    neighbor = selected;
    const u64 old_tail_hash = neighbor->raw_hash;
    const u64 ship_hash = ww_host_rel_live(host, 168u)->raw_hash;
    scenario = strcmp(requested, "enabled") == 0 ? "success" : requested;
    injected = false;
    ikada_call = true;
    selected = NULL;
    mem_write32(&host->cpu, 0x80002514u, 0x807FE000u);
    stop_before_entry = is("link-only") || is("skipped-prepare") || is("uninitialized-execution");
    run_call(host, 0x80002500u, "d_a_obj_ikada");
    if (is("raw-identity") || is("materialize-allocation") || is("materialize-trace")) {
        assert(host->error != NULL && selected == NULL && host->rel_image_count == 3u && host->alias_count == 3u);
        assert(neighbor->raw_hash == old_tail_hash && ww_host_rel_live(host, 60u) == NULL);
        return;
    }
    assert(selected != NULL && selected->module_id == 60u && selected->guest == 0x818327E0u);
    assert(selected->linked && !selected->unlinked && !host->rel_test_admission);
    if (stop_before_entry) {
        if (is("link-only")) host->rel_ikada_admission = false;
        else if (is("uninitialized-execution")) host->cpu.pc = 0xC1550154u;
        else selected->sdk_phase = WW_REL_SDK_NONE;
        assert(ww_host_dispatch(host, host->cpu.pc) == -1);
    }
    const bool positive = is("success") || is("observations") || strncmp(scenario, "interrupt-", 10u) == 0 ||
        is("epilog") || is("unresolved") || is("unlink") || is("duplicate-owner");
    if (positive) {
        if (host->error != NULL) fprintf(stderr, "ikada: %s pc=%08X\n", host->error, host->cpu.pc);
        assert(host->error == NULL && selected->initialization_completed && host->cpu.pc == 0x80001004u);
        assert(selected->raw_hash == lifecycle_expected_ikada_hash && selected->sdk_phase == WW_REL_SDK_CERTIFIED);
        assert(!selected->sdk_fixed && selected->sdk_input_form == WW_REL_SDK_OWNER_PUBLISHED);
        assert(selected->sdk_bss == 0x807FE000u && selected->sdk_previous == neighbor->guest);
        assert(neighbor->sdk_next == selected->guest && neighbor->raw_hash == lifecycle_expected_title_with_ikada_hash);
        assert(host->rel_sdk_tail == selected->guest && ww_host_rel_sdk_integrity(host));
        assert(mem_read32(&host->cpu, 0xC15581A0u) == 1u && mem_read32(&host->cpu, 0xC15581A4u) == 0u);
        assert(mem_read32(&host->cpu, 0x80005000u) == 4u);
        assert(host->cpu.gpr[1] == 0x80004000u && host->cpu.gpr[2] == 0x80300000u &&
               host->cpu.gpr[13] == 0x80400000u && host->cpu.gpr[31] == 0x12345678u);
        if (is("observations")) assert(entry_yields == 2u && return_yields == 2u);
        if (strncmp(scenario, "interrupt-", 10u) == 0) assert(handoffs == restorations && handoffs != 0u);
        if (is("duplicate-owner")) {
            assert(ww_host_materialize_rel(host, 0x80002600u, "d_a_obj_ikada") != NULL);
            assert(ww_host_rel_owned(host, selected->owner) == selected && host->rel_image_count == 4u);
        }
        if (is("unlink")) assert(ww_host_unlink_rel(host, selected->owner) != NULL);
        if (is("epilog") || is("unresolved")) {
            host->cpu.pc = is("epilog") ? 0xC1550120u : 0xC155014Cu;
            assert(ww_host_dispatch(host, host->cpu.pc) == -1);
            assert(strcmp(host->error, "REL epilog or unresolved entry has no supported lifecycle contract") == 0);
        }
    } else {
        if (host->error == NULL) fprintf(stderr, "ikada negative did not stop: %s pc=%08X\n", scenario, host->cpu.pc);
        const bool after_completed_return = strncmp(scenario, "rfi-", 4u) == 0 || strncmp(scenario, "nested-", 7u) == 0;
        assert(host->error != NULL && selected->initialization_completed == after_completed_return);
        assert(host->rel_pending_init.phase == WW_REL_INIT_NONE && host->rel_constructor_owner == 0u);
        if (selected->sdk_phase != WW_REL_SDK_CERTIFIED) {
            assert(selected->raw_hash == ww_host_rel_raw_hash(ikada_raw, sizeof(ikada_raw)));
            assert(neighbor->raw_hash == old_tail_hash && neighbor->sdk_next == 0u && host->rel_sdk_tail == neighbor->guest);
        }
    }
    assert(ww_host_rel_live(host, 168u)->raw_hash == ship_hash && profile->sdk_phase == WW_REL_SDK_RESIDENT);
    assert(host->alias_count == 4u && host->rel_image_count == 4u);
    assert(host->rel_title_block.image == neighbor && host->rel_profile_block.allocation.image == profile);
}
#endif

int main(int argc, char** argv) {
    assert(argc == 2);
    scenario = argv[1];
    const bool ikada_requested = strncmp(scenario, "ikada-", 6u) == 0;
    const bool resident = is("title-enabled") || is("title-copy") || is("title-catalog-id") || ikada_requested;
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRelSection profile_sections[] = {{1u,1u,0xC1F80000u,32u},{1u,4u,0xC1F80178u,16u},
                                                {1u,2u,0xC1F80170u,4u}};
    if (resident) profile_sections[0] = (StaticRecompRelSection){1u,1u,0xC1F800D4u,44u};
    StaticRecompRelModule descriptors[] = {fixture_rel,
        {.module_id=400u,.section_count=8u,.section_info_offset=0x4Cu,.file_size=sizeof(lifecycle_title_raw),
         .sections=lifecycle_title_sections,.num_sections=2u},
        {.module_id=1u,.section_count=5u,.section_info_offset=0x4Cu,.file_size=256u,
           .sections=profile_sections,.num_sections=resident ? 3u : 2u},
        {.module_id=60u,.section_count=21u,.section_info_offset=0x4Cu,.file_size=sizeof(ikada_raw),
         .sections=ikada_sections,.num_sections=6u}};
    StaticRecompRange chunks[] = {fixture_chunks[0],fixture_chunks[1],fixture_chunks[2],{0xC1E000DCu,0xC1E0013Cu},
                                 {0xC1F800D4u,0xC1F80100u},
                                 {0xC15500F4u,0xC1550154u},{0xC1550154u,0xC1550174u}};
    u64 hashes[] = {fixture_hashes[0],fixture_hashes[1],fixture_hashes[2],lifecycle_title_hash,lifecycle_profile_hash,
                    ikada_hashes[0],ikada_hashes[1]};
    StaticRecompModuleDesc module = {.code_ranges=lifecycle_ranges,
        .num_code_ranges=sizeof(lifecycle_ranges)/sizeof(lifecycle_ranges[0]), .chunk_ranges=chunks,
        .chunk_hashes=hashes,.num_chunk_ranges=ikada_requested ? 7u : resident ? 5u : 4u,
        .rel_modules=descriptors,.num_rel_modules=ikada_requested ? 4u : 3u,
        .dispatch=translated_dispatch};
    static const u8 profile_bytes[16];
    static const u8 profile_ctors[4];
    BlueWakeRelData data[] = {fixture_data[0],fixture_data[1],lifecycle_title_canonical[1],
                              {1u,4u,0xC1F80178u,16u,profile_bytes},{1u,2u,0xC1F80170u,4u,profile_ctors},
                              ikada_canonical[1],ikada_canonical[2],ikada_canonical[3],ikada_canonical[4],
                              {60u,6u,0xC15581A0u,532u,NULL}};
    host->module = &module;
    host->rel_data = data;
    host->rel_data_count = ikada_requested ? 10u : resident ? 5u : 4u;
    BlueWakeRelLifecycleV2 ship_lifecycle = {168u,0xC1B100F4u,0xC1B10120u,0xC1B1014Cu};
    BlueWakeRelLifecycleV2 title_lifecycle = {400u,0xC1E000DCu,0xC1E00108u,0xC1E00134u};
    BlueWakeRelLifecycleV2 ikada_lifecycle = {60u,0xC15500F4u,0xC1550120u,0xC155014Cu};
    BlueWakeRelInitContractV1 certificates[] = {
        {.module_id=168u,.rel_bytes=s_ship_contract.rel_bytes,.rel_size=s_ship_contract.rel_size,
         .canonical_sections=s_ship_contract.canonical_sections,.section_count=s_ship_contract.section_count,
         .ctor_table_section=4u,.ctor_return_pc=0xC1B1010Cu,.lifecycle=&ship_lifecycle,.build_identity=s_ship_contract.build_identity},
        {.module_id=400u,.rel_bytes=lifecycle_title_raw,.rel_size=sizeof(lifecycle_title_raw),
         .canonical_sections=lifecycle_title_canonical,.section_count=2u,.ctor_table_section=2u,
          .ctor_return_pc=0xC1E000F4u,.lifecycle=&title_lifecycle,.build_identity=s_ship_contract.build_identity},
        {.module_id=60u,.rel_bytes=ikada_raw,.rel_size=sizeof(ikada_raw),.canonical_sections=ikada_canonical,
         .section_count=5u,.ctor_table_section=2u,.ctor_return_pc=0xC155010Cu,.lifecycle=&ikada_lifecycle,
         .build_identity=s_ship_contract.build_identity}};
    BlueWakeRelInitContractSetV1 set = {1u,s_ship_contract.dol_size,s_ship_contract.dol_bytes,certificates,
                                       ikada_requested && !is("ikada-missing-60") ? 3u : 2u,s_ship_contract.build_identity};
    if (is("ikada-duplicate")) certificates[2].module_id = 168u;
    if (is("ikada-invalid-certificate")) certificates[2].ctor_return_pc = 0u;
    if (is("ikada-wrong-chunk-0")) hashes[5] ^= 1u;
    if (is("ikada-wrong-chunk-1")) hashes[6] ^= 1u;
    if (ikada_requested)
        for (u32 at = 1u; at < module.num_chunk_ranges; ++at)
            for (u32 previous = at; previous != 0u && chunks[previous].start < chunks[previous - 1u].start; --previous) {
                const StaticRecompRange range = chunks[previous];
                const u64 hash = hashes[previous];
                chunks[previous] = chunks[previous - 1u];
                hashes[previous] = hashes[previous - 1u];
                chunks[previous - 1u] = range;
                hashes[previous - 1u] = hash;
            }
    u8 catalog_raw[sizeof(lifecycle_title_raw)];
    StaticRecompRelSection catalog_sections[2];
    BlueWakeRelData catalog_canonical[2];
    if (is("title-catalog-id")) {
        memcpy(catalog_raw, lifecycle_title_raw, sizeof(catalog_raw));
        write_be32(catalog_raw, 401u);
        write_be32(catalog_raw + 0x130u, 401u);
        memcpy(catalog_sections, lifecycle_title_sections, sizeof(catalog_sections));
        memcpy(catalog_canonical, lifecycle_title_canonical, sizeof(catalog_canonical));
        for (u32 at = 0u; at < 2u; ++at) {
            catalog_sections[at].module_id = 401u;
            catalog_canonical[at].module_id = 401u;
        }
        descriptors[1].module_id = certificates[1].module_id = title_lifecycle.module_id = data[2].module_id = 401u;
        descriptors[1].sections = catalog_sections;
        certificates[1].rel_bytes = catalog_raw;
        certificates[1].canonical_sections = catalog_canonical;
    }
    const char* registration = ww_host_register_rel_contracts(host, &set, "synthetic.dol");
    if (is("ikada-duplicate") || is("ikada-invalid-certificate") || is("ikada-bad-dol") ||
        is("ikada-wrong-chunk-0") || is("ikada-wrong-chunk-1")) {
        assert(registration != NULL && host->rel_contracts == NULL);
#if WW05_ENABLE_IKADA_CONTRACT
        assert(!host->rel_ikada_admission);
#endif
        goto done;
    }
    if (registration != NULL) fprintf(stderr, "registration: %s\n", registration);
    assert(registration == NULL);
    if (ikada_requested) {
        const bool no_ship = is("ikada-registered-only") || is("ikada-title-only") || is("ikada-missing-ship");
        const bool no_title = is("ikada-registered-only") || is("ikada-ship-only") || is("ikada-missing-title");
        if (!no_ship)
            assert(ww_host_enable_ship_contract(host, &s_ship_contract, "synthetic.dol") == NULL);
        host->rel_title_admission = !no_title;
        if (is("ikada-missing-set")) host->rel_contracts = NULL;
#if WW05_ENABLE_IKADA_CONTRACT
        if (is("ikada-missing-ship") || is("ikada-missing-title") || is("ikada-missing-set") || is("ikada-missing-60")) {
            assert(ww_host_enable_ikada_contract(host) != NULL && !host->rel_ikada_admission);
            assert(ww_host_rel_certificate(host, 60u) == NULL && !host->rel_test_admission);
            goto done;
        }
#endif
        if (is("ikada-registered-only") || is("ikada-ship-only") || is("ikada-title-only") || is("ikada-off")) {
            assert(ww_host_rel_certificate(host, 60u) == NULL);
            const char* error = ww_host_materialize_rel(host, 0x80002500u, "d_a_obj_ikada");
            assert(error != NULL && strcmp(error, "REL prolog, epilog or unresolved entry requires an unsupported lifecycle contract") == 0);
            assert(host->rel_image_count == 0u && !host->rel_test_admission);
            goto done;
        }
#if WW05_ENABLE_IKADA_CONTRACT
        ikada_call = is("ikada-admission-trace");
        trace_failure = ikada_call;
        const char* error = ww_host_enable_ikada_contract(host);
        trace_failure = false;
        ikada_call = false;
        if (is("ikada-admission-trace")) {
            assert(error != NULL && !host->rel_ikada_admission && host->rel_image_count == 0u);
            goto done;
        }
        if (error != NULL) fprintf(stderr, "ikada admission: %s\n", error);
        assert(error == NULL && host->rel_ikada_admission && !host->rel_test_admission);
        assert(ww_host_rel_certificate(host, 60u) == &certificates[2]);
        assert(ww_host_rel_certificate(host, 168u) == &host->ship_certificate);
        assert(ww_host_rel_certificate(host, 400u) == &certificates[1]);
        assert(ww_host_rel_certificate(host, 61u) == NULL && ww_host_rel_certificate(host, 401u) == NULL);
#else
        assert(false);
#endif
    }
    const bool title_policy = strncmp(scenario, "title-", 6u) == 0;
#if WW05_ENABLE_TITLE_CONTRACT
    if (title_policy && !is("title-disabled"))
        host->rel_title_admission = true;
#endif
    if (is("title-enabled") || is("title-copy"))
        assert(ww_host_rel_certificate(host, 400u) == &certificates[1]);
    if (is("title-catalog-id"))
        assert(ww_host_rel_certificate(host, 401u) == NULL);
    if (!ikada_requested && !is("registered-only") && !is("ship-only") && !is("title-enabled"))
        assert(ww_host_rel_test_admission(host) == NULL);
    if (is("legacy") || is("ship-only") || title_policy)
        assert(ww_host_enable_ship_contract(host, &s_ship_contract, "synthetic.dol") == NULL);
    for (u32 at = 0u; at < host->rel_data_count; ++at)
        assert(ppc_guest_alias_add(data[at].linked_start, data[at].size, data[at].bytes));
    DolLayout layout = {0};
    lifecycle_dol_text(&host->cpu, &layout);
    assert(ww_code_guard_init(&host->code_guard, &host->cpu, &layout));
    assert(ww_host_materialize_rel(host, 0x80002400u, "f_pc_profile_lst") == NULL);
    host->cpu.lr = 0x80001004u;
    assert(ww_host_link_rel(host, 0x80002400u) == NULL);
    WwRelImage* profile = ww_host_rel_live(host, 1u);
    assert(profile != NULL);
    if (resident) {
        CPUState* cpu = &host->cpu;
        requested_owner = profile->owner;
        profile_call = true;
        cpu->pc = 0x80240C9Cu;
        cpu->gpr[1] = 0x80003FF0u;
        cpu->gpr[2] = 0x80300000u;
        cpu->gpr[13] = 0x80400000u;
        cpu->gpr[31] = profile->owner;
        mem_write32(cpu, 0x80003FFCu, 0x12345678u);
        mem_write32(cpu, 0x80004004u, 0x80001004u);
        for (u32 turn = 0u; turn < 128u && cpu->pc != 0x80001004u && host->error == NULL; ++turn) {
            ww_host_end_dispatch(host);
            bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
            cpu->cycle_budget = 80;
            cpu->downcount = 0;
            if (!ww_host_edge(host, cpu, cpu->pc))
                assert(ww_host_dispatch(host, cpu->pc) != 0);
        }
        if (host->error != NULL || !profile->initialization_completed)
            fprintf(stderr, "resident completion: %s pc=%08X lr=%08X\n",
                    host->error == NULL ? "incomplete" : host->error, cpu->pc, cpu->lr);
        assert(host->error == NULL && profile->initialization_completed && profile->certificate == NULL);
        assert(!host->rel_sdk_seeded && host->rel_pending_init.phase == WW_REL_INIT_NONE);
        profile_call = false;
    } else {
        mem_write32(&host->cpu, 0x800030C8u, profile->guest);
        mem_write32(&host->cpu, 0x800030CCu, profile->guest);
    }
    mem_write32(&host->cpu, 0x80002214u, 0x807FF620u);
    if (is("registered-only") || is("ship-only")) {
        run_call(host, 0x80002300u, "d_a_title");
        assert(host->error != NULL && ww_host_rel_owned(host, 0x80002300u) == NULL);
        assert(strcmp(host->error, "REL prolog, epilog or unresolved entry requires an unsupported lifecycle contract") == 0);
        assert(host->alias_count == 1u && host->rel_pending_init.phase == WW_REL_INIT_NONE);
        goto done;
    }
    const char* requested = scenario;
    scenario = "success";
    run_call(host, 0x80002200u, "d_a_ship");
    if (host->error != NULL) fprintf(stderr, "ship: %s pc=%08X lr=%08X r3=%08X r31=%08X phase=%u\n",
        host->error, host->cpu.pc, host->cpu.lr, host->cpu.gpr[3],host->cpu.gpr[31],selected->sdk_phase);
    assert(host->error == NULL && host->cpu.pc == 0x80001004u);
    neighbor = ww_host_rel_live(host, 168u);
    assert(neighbor != NULL && neighbor->initialization_completed);
    assert(neighbor->guest == 0x81820100u && neighbor->raw_hash ==
        (resident ? lifecycle_expected_ship_nonfixed_hash : lifecycle_expected_ship_hash));
    assert(mem_read32(&host->cpu, 0xC1B224D0u) == 2u && mem_read32(&host->cpu, 0x80005000u) == 1u + (u32)resident);
    assert(host->rel_pending_init.phase == WW_REL_INIT_NONE && host->rel_constructor_owner == 0u);
    scenario = requested;
#if WW05_ENABLE_IKADA_CONTRACT
    if (ikada_requested) {
        run_ikada(host, requested + 6u, profile);
        goto done;
    }
#endif
    selected = NULL;
    injected = false;
    if (is("legacy")) goto done;
    stop_before_entry = is("pristine") || is("partial") || is("failed-sdk") || is("walker-without-active") || is("no-pending");
    stop_before_entry |= is("entry-certified-r3");
    run_call(host, 0x80002300u, "d_a_title");
    assert(selected != NULL);
    assert(selected->guest == 0x818325E0u);
    if (title_policy) {
        assert(!host->rel_test_admission);
        if (!is("title-enabled")) {
            assert(host->error != NULL && !selected->initialization_completed);
            assert(strcmp(host->error, is("title-disabled") ? "title initialization is disabled" :
                "REL prolog entry requires its exact translated dynamic-link caller") == 0);
            assert(host->rel_pending_init.phase == WW_REL_INIT_NONE && host->rel_constructor_owner == 0u);
            assert(mem_read32(&host->cpu, 0x80005000u) == 1u + (u32)resident);
            goto done;
        }
    }
    if (strstr(scenario, "tamper-certificate") == NULL)
        assert(selected->certificate == &certificates[1]);
    if (stop_before_entry) {
        CPUState* cpu = &host->cpu;
        if (is("entry-certified-r3")) {
            assert(ww_host_rel_sdk_finish(host, selected) == NULL);
            cpu->gpr[3] += 4u;
        } else if (is("no-pending")) {
            assert(ww_host_rel_sdk_finish(host, selected) == NULL);
            cpu->pc = cpu->lr = 0x80240EE8u;
        } else if (is("walker-without-active")) {
            assert(ww_host_rel_sdk_finish(host, selected) == NULL);
            cpu->pc = 0x80241178u;
            cpu->lr = 0xC1E000F4u;
            cpu->gpr[3] = 0xC1E01F28u;
        } else {
            if (is("pristine")) {
                memcpy(get_ram_ptr(cpu, selected->guest, selected->bytes, NULL), lifecycle_title_raw, selected->bytes);
                selected->sdk_phase = WW_REL_SDK_NONE;
            } else {
                mem_write32(cpu, selected->guest + 0x48u, 0x110u);
                if (is("failed-sdk")) assert(ww_host_rel_sdk_finish(host, selected) != NULL);
            }
        }
        assert(ww_host_dispatch(host, cpu->pc) == -1);
    }
    const bool positive = is("success") || is("title-enabled") || is("observations") || strncmp(scenario, "interrupt-", 10u) == 0 ||
        is("expiry") || is("repeat-init") || strncmp(scenario, "epilog-", 7u) == 0 ||
        strncmp(scenario, "unresolved-", 11u) == 0 || is("unlink") || is("native-unlink");
    if (positive) {
        if (host->error != NULL || host->cpu.pc != 0x80001004u)
            fprintf(stderr, "lifecycle: %s pc=%08X exception=%08X budget=%lld downcount=%lld\n",
                    host->error == NULL ? "incomplete" : host->error, host->cpu.pc, host->cpu.exception,
                    (long long)host->cpu.cycle_budget, (long long)host->cpu.downcount);
        assert(host->error == NULL && host->cpu.pc == 0x80001004u);
        assert(selected->initialization_completed && neighbor->initialization_completed && profile->initialization_completed == resident);
        assert(host->rel_pending_init.phase == WW_REL_INIT_NONE && host->rel_constructor_owner == 0u);
        assert(mem_read32(&host->cpu, 0x80005000u) == 2u + (u32)resident && mem_read32(&host->cpu, 0xC1B224D0u) == 2u);
        assert(ww_host_rel_sdk_integrity(host) && selected->sdk_previous == neighbor->guest && neighbor->sdk_next == selected->guest);
        assert(host->cpu.gpr[1] == 0x80004000u && host->cpu.gpr[2] == 0x80300000u &&
               host->cpu.gpr[13] == 0x80400000u && host->cpu.gpr[31] == 0x12345678u);
        if (is("observations")) assert(entry_yields == 2u && return_yields == 2u);
        if (strncmp(scenario, "interrupt-", 10u) == 0) assert(handoffs == restorations && handoffs != 0u);
        if (is("unlink")) assert(ww_host_unlink_rel(host, selected->owner) != NULL);
        if (is("expiry") || is("repeat-init") || is("native-unlink") ||
            strncmp(scenario, "epilog-", 7u) == 0 || strncmp(scenario, "unresolved-", 11u) == 0) {
            CPUState* cpu = &host->cpu;
            cpu->pc = is("expiry") ? 0x80240EE8u : is("repeat-init") ? title_lifecycle.prolog_address :
                is("native-unlink") ? 0x80240F54u : strncmp(scenario, "epilog-", 7u) == 0 ?
                title_lifecycle.epilog_address : title_lifecycle.unresolved_address;
            if (strstr(scenario, "scratch") != NULL)
                cpu->pc = selected->guest + 0xA0u + (strncmp(scenario, "epilog-", 7u) == 0 ? 44u : 88u);
            cpu->lr = 0x80240EE8u;
            cpu->gpr[3] = is("native-unlink") ? selected->owner : selected->guest;
            cpu->gpr[31] = selected->owner;
            assert(ww_host_dispatch(host, cpu->pc) == -1 && host->error != NULL);
        }
    } else {
        assert(host->error != NULL && host->rel_pending_init.phase == WW_REL_INIT_NONE);
        assert(host->rel_constructor_owner == 0u && !ww_host_rel_return_qualified(host, 0x80240EE8u));
        if ((strncmp(scenario, "entry-", 6u) == 0 && !is("entry-after-walker")) ||
            strncmp(scenario, "duplicate-", 10u) == 0 ||
            is("partial") || is("pristine") || is("failed-sdk") || is("walker-without-active") ||
            is("no-pending") || is("walker-table") || is("walker-successor") || is("ctor-terminator") ||
            is("entry-after-progress") || is("return-without-walker") || strncmp(scenario, "active-tamper-", 14u) == 0)
            assert(mem_read32(&host->cpu, 0x80005000u) == 1u + (u32)resident);
        if (is("rfi-misaligned")) {
            assert(host->cpu.srr0 == 0x80240EE9u && host->cpu.pc != 0x80240EE8u);
            assert(host->cpu.msr != (PPC_MSR_FP | PPC_MSR_EE));
        }
        if (is("rfi-fp") || is("rfi-ee") || is("rfi-no-stage") || is("rfi-outside-dol"))
            assert(host->cpu.pc != 0x80240EE8u);
        if (is("entry-certified-r3") || is("entry-trace") || is("completion-trace") || is("return-trace")) {
            assert(ww_host_rel_sdk_integrity(host));
            assert(!selected->initialization_completed);
        }
    }
    assert(selected->linked && !selected->unlinked && neighbor->linked && profile->linked);
    const bool certified = selected->sdk_phase == WW_REL_SDK_CERTIFIED;
    assert(selected->raw_hash == (certified ? lifecycle_expected_title_hash : lifecycle_expected_title_pristine_hash));
    assert(neighbor->raw_hash == (resident ?
        (certified ? lifecycle_expected_ship_nonfixed_with_title_hash : lifecycle_expected_ship_nonfixed_hash) :
        (certified ? lifecycle_expected_ship_with_title_hash : lifecycle_expected_ship_hash)));
    assert(host->rel_sdk_tail == (certified ? selected->guest : neighbor->guest));
    u8* storage = NULL;
    assert(ppc_guest_alias_get_storage(selected->guest, selected->capacity, &storage) && storage != NULL);
    assert(host->alias_count == 3u && host->rel_image_count == 3u);
    assert(mem_read32(&host->cpu, 0x803F6A68u) == 0xC1F80178u);
done:
    trace_failure = false;
    ww_host_shutdown(host);
    free(host);
    return 0;
}
