#include "boot_host.h"
#include "probe_support.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void* __real_calloc(size_t count, size_t size);
static unsigned allocation;
static unsigned fail_allocation;
static u32 exi_logs;
static u32 fail_exi_log;
void* __wrap_calloc(size_t count, size_t size) {
    if (++allocation == fail_allocation)
        return NULL;
    return __real_calloc(count, size);
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && strncmp(format, "[exi]", 5u) == 0 && ++exi_logs == fail_exi_log) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int dispatch(CPUState* cpu, u32 address) {
    (void)cpu;
    (void)address;
    return 0;
}

static StaticRecompModuleDesc synthetic_module(void) {
    static StaticRecompRange ranges[] = {{0x80001000u, 0x80001004u}, {0xC0401000u, 0xC0401100u}};
    static StaticRecompRelSection sections[] = {
        {.module_id = 1u, .section_index = 1u, .linked_start = 0xC0401000u, .size = 256u},
        {.module_id = 1u, .section_index = 2u, .linked_start = 0xC0403000u, .size = 8u},
        {.module_id = 336u, .section_index = 6u, .linked_start = 0x81500000u, .size = 256u},
    };
    static StaticRecompRelModule rel[] = {{.num_sections = 3u, .sections = sections}};
    StaticRecompModuleDesc module = {.abi_version = STATICRECOMP_ABI_VERSION,
        .cpu_abi_version = GXRUNTIME_CPU_ABI_VERSION, .cpu_state_size = sizeof(CPUState),
        .game_id = "GZLE01", .entry_point = 0x80001000u, .dispatch = dispatch,
        .code_ranges = ranges, .num_code_ranges = 2u, .rel_modules = rel, .num_rel_modules = 1u};
    return module;
}

static void synthetic_dol(void) {
    u8 bytes[264] = {0};
    write_be32(bytes, 256u);
    write_be32(bytes + 4u, 260u);
    write_be32(bytes + 0x48u, 0x80001000u);
    write_be32(bytes + 0x4Cu, 0x80001100u);
    write_be32(bytes + 0x90u, 4u);
    write_be32(bytes + 0x94u, 4u);
    write_be32(bytes + 0xD8u, 0x80002000u);
    write_be32(bytes + 0xDCu, 256u);
    write_be32(bytes + 0xE0u, 0x80001000u);
    FILE* stream = fopen("synthetic.dol", "wb");
    assert(stream != NULL);
    assert(fwrite(bytes, 1u, sizeof bytes, stream) == sizeof bytes);
    assert(fclose(stream) == 0);
}

static void exi_sram_command(WwHost* host, u32 command) {
    host->cpu.pc = 0x80309E1Cu;
    host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, 0x130u, 4u);
    host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF, command, 4u);
    host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 0x35u, 4u);
}

static void expect_decrementer_delivery(WwHost* host) {
    const u32 pc = host->cpu.pc;
    const u32 msr = host->cpu.msr;
    assert(ww_host_edge(host, &host->cpu, pc));
    assert(host->error == NULL && !host->edge_stopped);
    assert(host->cpu.pc == 0x900u);
    assert(host->cpu.srr0 == pc);
    assert(host->cpu.srr1 == (msr & PPC_MSR_RFI_MASK));
    assert(!(host->cpu.msr & PPC_MSR_EE));
    assert(host->cpu.exception == 0u && !host->decrementer_pending);
}

static void probe_decrementer(WwHost* host, const char* scenario) {
    CPUState* cpu = &host->cpu;
    cpu->pc = 0x80001000u;
    if (strcmp(scenario, "dec-reset") == 0) {
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == UINT32_MAX);
        charge_cycles(host, 12u);
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == UINT32_MAX - 1u);
        assert(cpu->timebase == 1u && !host->decrementer_pending);
    } else if (strcmp(scenario, "dec-fallback") == 0) {
        cpu->gpr[3] = 7u;
        ppc_fallback_instruction(cpu, 0x7C7603A6u, cpu->pc);
        assert(cpu->pc == 0x80001004u);
        charge_cycles(host, 12u);
        ppc_fallback_instruction(cpu, 0x7C7602A6u, cpu->pc);
        assert(cpu->gpr[3] == 6u && cpu->pc == 0x80001008u);
    } else if (strcmp(scenario, "dec-fractional") == 0) {
        ppc_mtspr(cpu, 22u, 2u, cpu->pc);
        charge_cycles(host, 5u);
        charge_cycles(host, 6u);
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == 2u && cpu->timebase == 0u);
        charge_cycles(host, 1u);
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == 1u && cpu->timebase == 1u);
    } else if (strcmp(scenario, "dec-threshold") == 0) {
        ppc_mtspr(cpu, 22u, 1u, cpu->pc);
        charge_cycles(host, 12u);
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == 0u && !host->decrementer_pending);
        charge_cycles(host, 12u);
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == UINT32_MAX && host->decrementer_pending);
        assert(cpu->exception == 0u);
    } else if (strcmp(scenario, "dec-wrap") == 0) {
        ppc_mtspr(cpu, 22u, 5u, cpu->pc);
        charge_cycles(host, ((1ull << 32u) + 7u) * 12u);
        assert(ppc_mfspr(cpu, 22u, cpu->pc) == UINT32_MAX - 1u);
        assert(cpu->timebase == (1ull << 32u) + 7u && host->decrementer_pending);
    } else if (strcmp(scenario, "dec-negative-write") == 0) {
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        ppc_mtspr(cpu, 22u, 0x80000000u, cpu->pc);
        assert(host->decrementer_pending && cpu->exception == 0u);
    } else if (strcmp(scenario, "dec-negative-rewrite") == 0) {
        ppc_mtspr(cpu, 22u, UINT32_MAX, cpu->pc);
        ppc_mtspr(cpu, 22u, 0x80000000u, cpu->pc);
        assert(!host->decrementer_pending && cpu->exception == 0u);
    } else if (strcmp(scenario, "dec-positive-rewrite") == 0) {
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        charge_cycles(host, 12u);
        ppc_mtspr(cpu, 22u, 100u, cpu->pc);
        assert(host->decrementer_pending && host->decrementer == 100u);
    } else if (strcmp(scenario, "dec-deadline") == 0) {
        ppc_mtspr(cpu, 22u, 2u, cpu->pc);
        charge_cycles(host, 5u);
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        assert(cpu->cycle_budget == 31 && cpu->cycle_deadline_active);
        cpu->downcount = -31;
        bluewake_cycle_domain_end_turn(&host->cycles, cpu);
        assert(host->decrementer_pending && cpu->timebase == 3u);
    } else if (strcmp(scenario, "dec-disabled") == 0) {
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        charge_cycles(host, 12u);
        assert(!ww_host_edge(host, cpu, cpu->pc));
        assert(cpu->pc == 0x80001000u && host->decrementer_pending);
        cpu->msr |= PPC_MSR_EE;
        expect_decrementer_delivery(host);
    } else if (strcmp(scenario, "dec-edge-charge") == 0) {
        ppc_mtspr(cpu, 22u, 9u, cpu->pc);
        cpu->msr = PPC_MSR_EE;
        cpu->downcount = -120;
        cpu->cycle_observation_suffix = 24u;
        expect_decrementer_delivery(host);
        assert(cpu->timebase == 10u && host->cycles.absolute_cycles == 120u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 0u);
    } else if (strcmp(scenario, "dec-turn-credit") == 0) {
        ppc_mtspr(cpu, 22u, 1000u, cpu->pc);
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        cpu->downcount = -120;
        assert(!ww_host_edge(host, cpu, cpu->pc));
        assert(host->cycles.dispatch_cycles == 120u && cpu->downcount == 0);
        assert(cpu->cycle_budget == 136);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 0u);
    } else if (strcmp(scenario, "dec-mmio-quantum") == 0) {
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        u32 reads = 0u;
        while (cpu->downcount > -cpu->cycle_budget) {
            cpu->downcount -= 1;
            cpu->cycle_observation_suffix = 0u;
            assert(cpu->external_read(cpu, 0xCC00500Au, 2u) == 0x0804u);
            assert(++reads <= 256u);
        }
        assert(reads == 256u && cpu->cycle_budget == 0 && cpu->downcount == 0);
        assert(host->cycles.absolute_cycles == 256u && cpu->timebase == 21u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 0u);
    } else if (strcmp(scenario, "dec-mmio-next-turn") == 0) {
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        cpu->downcount = -256;
        assert(cpu->external_read(cpu, 0xCC00500Au, 2u) == 0x0804u);
        assert(cpu->cycle_budget == 0);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 0u);
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        assert(cpu->cycle_budget == 256 && cpu->downcount == 0);
        assert(host->cycles.absolute_cycles == 256u && host->cycles.dispatch_cycles == 0u);
    } else if (strcmp(scenario, "dec-rfi") == 0) {
        const u32 msr = PPC_MSR_EE | PPC_MSR_FP | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_RI;
        const u32 pi_cause = host->interrupts.pi_cause;
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        charge_cycles(host, 12u);
        cpu->msr = msr;
        expect_decrementer_delivery(host);
        assert(host->interrupts.pi_cause == pi_cause && host->interrupts.pi_mask == 0u);
        assert(cpu->msr == 0u);
        ppc_rfi(cpu, 0x80000940u);
        assert(cpu->pc == 0x80001000u && cpu->msr == msr);
        assert(!ww_host_edge(host, cpu, cpu->pc));
    } else if (strcmp(scenario, "dec-external-priority") == 0) {
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        charge_cycles(host, 12u);
        cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_SI, 4u);
        cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010301u, 4u);
        cpu->msr = PPC_MSR_EE;
        assert(ww_host_edge(host, cpu, cpu->pc));
        assert(cpu->pc == 0x500u && host->decrementer_pending);
        cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
        ppc_rfi(cpu, 0x80000540u);
        expect_decrementer_delivery(host);
    } else if (strcmp(scenario, "dec-prior-fault") == 0) {
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        charge_cycles(host, 12u);
        cpu->msr = PPC_MSR_EE;
        cpu->exception = PPC_EXC_PROGRAM;
        assert(!ww_host_edge(host, cpu, cpu->pc));
        assert(cpu->pc == 0x80001000u && cpu->exception == PPC_EXC_PROGRAM);
        assert(host->decrementer_pending);
    } else if (strcmp(scenario, "dec-zero-charge") == 0) {
        ppc_mtspr(cpu, 22u, 0u, cpu->pc);
        cpu->msr = PPC_MSR_EE;
        for (u32 index = 0u; index < 16u; ++index) {
            charge_cycles(host, 0u);
            assert(!ww_host_edge(host, cpu, cpu->pc));
        }
        assert(cpu->timebase == 0u && host->decrementer == 0u && !host->decrementer_pending);
    } else if (strcmp(scenario, "dec-read-privilege") == 0) {
        cpu->msr = PPC_MSR_PR;
        ppc_mfspr(cpu, 22u, cpu->pc);
        assert(cpu->exception == PPC_EXC_PROGRAM);
        assert(cpu->program_exception == PPC_PROGRAM_PRIV);
    } else if (strcmp(scenario, "dec-write-privilege") == 0) {
        cpu->msr = PPC_MSR_PR;
        ppc_mtspr(cpu, 22u, 7u, cpu->pc);
        assert(cpu->exception == PPC_EXC_PROGRAM);
        assert(cpu->program_exception == PPC_PROGRAM_PRIV);
        assert(host->decrementer == UINT32_MAX);
    } else {
        assert(!"unknown decrementer scenario");
    }
    assert(host->error == NULL);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL);
    allocation = 0u;
    if (strncmp(argv[1], "allocation-", 11u) == 0) {
        fail_allocation = (unsigned)strtoul(argv[1] + 11u, NULL, 10);
        assert(!ww_host_init(host));
        assert(host->cpu.ram == NULL);
        assert(host->memory.vm == NULL);
        assert(host->memory.locked_cache == NULL);
        free(host);
        return 0;
    }
    assert(ww_host_init(host));
    if (strcmp(argv[1], "unknown-mmio") == 0) {
        host->cpu.pc = 0x80001000u;
        host->cpu.external_read(&host->cpu, 0xCC007000u, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC007000u);
        assert(host->error_pc == 0x80001000u);
        assert(ww_host_edge(host, &host->cpu, host->cpu.pc));
    } else if (strcmp(argv[1], "pvr") == 0) {
        assert(ppc_mfspr(&host->cpu, 287u, 0x80001000u) == 0x00083214u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "unknown-spr") == 0) {
        ppc_mfspr(&host->cpu, 999u, 0x80001000u);
        assert(host->error != NULL);
        assert(host->error_address == 999u);
    } else if (strcmp(argv[1], "l2-reset") == 0) {
        host->cpu.pc = 0x803014ECu;
        host->cpu.instruction_fallback(&host->cpu, 0x7C79FAA6u, host->cpu.pc);
        assert(host->cpu.gpr[3] == 0u);
        assert(host->cpu.pc == 0x803014F0u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "l2-enable") == 0) {
        ppc_mtspr(&host->cpu, 1017u, 0x80000000u, 0x803014F4u);
        assert(ppc_mfspr(&host->cpu, 1017u, 0x803014ECu) == 0x80000000u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "l2-invalidate-empty") == 0) {
        mem_write32(&host->cpu, 0x80001000u, 0x12345678u);
        host->cpu.pc = 0x803014F4u;
        host->cpu.gpr[3] = 0x00200000u;
        host->cpu.instruction_fallback(&host->cpu, 0x7C79FBA6u, host->cpu.pc);
        assert(host->cpu.pc == 0x803014F8u);
        assert(ppc_mfspr(&host->cpu, 1017u, 0x803014ECu) == 0x00200000u);
        assert(mem_read32(&host->cpu, 0x80001000u) == 0x12345678u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "l2-unsupported") == 0) {
        ppc_mtspr(&host->cpu, 1017u, 2u, 0x803014F4u);
        assert(host->error != NULL);
        assert(host->error_address == 1017u);
        assert(host->error_pc == 0x803014F4u);
        assert(ppc_mfspr(&host->cpu, 1017u, 0x803014ECu) == 0u);
    } else if (strcmp(argv[1], "l2-privilege") == 0) {
        host->cpu.msr |= PPC_MSR_PR;
        ppc_mtspr(&host->cpu, 1017u, 0x80000000u, 0x803014F4u);
        assert(host->cpu.exception & PPC_EXC_PROGRAM);
        host->cpu.msr &= ~PPC_MSR_PR;
        assert(ppc_mfspr(&host->cpu, 1017u, 0x803014ECu) == 0u);
    } else if (strcmp(argv[1], "aram-config-defaults") == 0) {
        assert(host->cpu.external_read(&host->cpu, 0xCC005012u, 2u) == 0u);
        assert(host->cpu.external_read(&host->cpu, 0xCC005016u, 2u) == 1u);
        assert(host->cpu.external_read(&host->cpu, 0xCC00501Au, 2u) == 156u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "aram-info-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC005012u, UINT16_MAX, 2u);
        assert(host->cpu.external_read(&host->cpu, 0xCC005012u, 2u) == 0x007Fu);
        assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0804u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "aram-refresh-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00501Au, UINT16_MAX, 2u);
        assert(host->cpu.external_read(&host->cpu, 0xCC00501Au, 2u) == 0x07FFu);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "aram-mode-read-only") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC005016u, 0u, 2u);
        assert(host->error != NULL && host->error_address == 0xCC005016u);
        assert(host->cpu.external_read(&host->cpu, 0xCC005016u, 2u) == 1u);
    } else if (strcmp(argv[1], "aram-config-width") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC005012u, 0x12345678u, 4u);
        assert(host->error != NULL && host->error_address == 0xCC005012u);
        assert(host->error_width == 4u && host->error_value == 0x12345678u);
        assert(host->cpu.external_read(&host->cpu, 0xCC005012u, 2u) == 0u);
    } else if (strncmp(argv[1], "dec-", 4u) == 0) {
        probe_decrementer(host, argv[1]);
    } else if (strcmp(argv[1], "clock") == 0) {
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount = -120;
        assert(ppc_mfspr(&host->cpu, 268u, 0x80001000u) == 10u);
        assert(host->cycles.absolute_cycles == 120u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
    } else if (strcmp(argv[1], "frontier") == 0) {
        host->cpu.pc = 0x80001000u;
        host->cpu.gpr[1] = 0x817FFF00u;
        host->cpu.external_write(&host->cpu, 0xCC008000u, 0x61u, 1u);
        assert(!ww_host_edge(host, &host->cpu, host->cpu.pc));
        host->cpu.external_write(&host->cpu, 0xCC008000u, 0xFEFFFFFFu, 4u);
        assert(ww_host_edge(host, &host->cpu, host->cpu.pc));
        assert(host->edge_stopped);
        assert(host->frontier.complete);
    } else if (strcmp(argv[1], "instruction") == 0) {
        host->cpu.pc = 0x80001000u;
        host->cpu.instruction_fallback(&host->cpu, 0xFFFFFFFFu, host->cpu.pc);
        assert(host->cpu.pc == 0x80001000u);
        assert(host->error != NULL);
    } else if (strncmp(argv[1], "epilogue-edge-", 14u) == 0) {
        const bool context = strcmp(argv[1], "epilogue-edge-context") == 0 ||
                             strcmp(argv[1], "epilogue-edge-context-unowned") == 0;
        const bool compiled = strcmp(argv[1], "epilogue-edge-compiled") == 0 ||
                              strcmp(argv[1], "epilogue-edge-context") == 0;
        const bool unowned = strcmp(argv[1], "epilogue-edge-unowned") == 0 ||
                             strcmp(argv[1], "epilogue-edge-context-unowned") == 0;
        const bool service = strcmp(argv[1], "epilogue-edge-service") == 0;
        assert(compiled || unowned || service);
        StaticRecompRange range = service ? (StaticRecompRange){0x803193ACu, 0x803193B0u} :
                                  context ? (StaticRecompRange){0x80303A50u, 0x80303B28u} :
                                            (StaticRecompRange){0x80328F84u, 0x80328F9Cu};
        StaticRecompModuleDesc module = {.dispatch = dispatch, .code_ranges = &range, .num_code_ranges = 1u};
        host->module = unowned ? NULL : &module;
        host->cpu.pc = range.start;
        host->cpu.lr = 0x8030141Cu;
        host->cpu.gpr[11] = 0x8040CF60u;
        host->cpu.gpr[3] = 0x803A2960u;
        host->cpu.srr0 = 0x802AE070u;
        host->cpu.srr1 = 0x0000A032u;
        for (u32 reg = 27u; reg <= 31u; ++reg)
            host->cpu.gpr[reg] = 0xA5000000u | reg;
        const CPUState before = host->cpu;
        assert(ww_host_edge(host, &host->cpu, host->cpu.pc) == !compiled);
        assert((host->error == NULL) == compiled && host->edge_stopped == !compiled);
        assert(host->cpu.pc == before.pc && host->cpu.lr == before.lr && host->cpu.msr == before.msr);
        assert(host->cpu.srr0 == before.srr0 && host->cpu.srr1 == before.srr1);
        assert(memcmp(host->cpu.gpr, before.gpr, sizeof(before.gpr)) == 0);
        assert(memcmp(host->cpu.gqr, before.gqr, sizeof(before.gqr)) == 0);
        assert(memcmp(host->cpu.fpr, before.fpr, sizeof(before.fpr)) == 0 && host->cpu.fpscr == before.fpscr);
        assert(host->cycles.absolute_cycles == 0u);
        if (!compiled)
            assert(host->error_address == before.pc && host->error_pc == before.pc);
    } else if (strncmp(argv[1], "fp-credit-", 10u) == 0) {
        const bool prepaid = strcmp(argv[1], "fp-credit-prepaid") == 0;
        const bool precise = strcmp(argv[1], "fp-credit-precise") == 0;
        const bool invalid = strcmp(argv[1], "fp-credit-invalid") == 0;
        const bool other = strcmp(argv[1], "fp-credit-other") == 0;
        assert(prepaid || precise || invalid || other);
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.msr = 0u;
        assert(!ppc_fp_available(&host->cpu, 0x80001234u));
        const s64 budget = host->cpu.cycle_budget;
        host->cpu.downcount = prepaid || other ? -17 : -1;
        host->cpu.cycle_observation_suffix = precise ? 0u : 16u;
        if (other)
            host->cpu.exception = PPC_EXC_PROGRAM;
        assert(ww_host_end_dispatch(host) == (other ? 17u : 1u));
        assert(host->cycles.absolute_cycles == (other ? 17u : 1u));
        assert((host->error != NULL) == invalid);
        if (invalid)
            assert(host->error_address == 0x80001234u && host->error_pc == 0x800u);
        assert(host->cpu.pc == 0x800u && host->cpu.srr0 == 0x80001234u && host->cpu.msr == 0u);
        assert(host->cpu.cycle_observation_suffix == (other ? 16u : 0u));
        assert(host->cpu.cycle_budget == budget && host->cpu.downcount == 0);
        assert(ww_host_end_dispatch(host) == 0u);
        assert((host->error != NULL) == invalid);
    } else if (strncmp(argv[1], "sprg-state-", 11u) == 0) {
        const u16 spr = (u16)strtoul(argv[1] + 11u, NULL, 10);
        assert(spr == 272u);
        assert(ppc_mfspr(&host->cpu, spr, 0x500u) == 0u && host->error == NULL);
        ppc_mtspr(&host->cpu, spr, 0xB6B6B6B6u, 0x500u);
        assert(ppc_mfspr(&host->cpu, spr, 0x504u) == 0xB6B6B6B6u && host->error == NULL);
        assert(host->cycles.absolute_cycles == 0u && host->cpu.timebase == 0u);
    } else if (strcmp(argv[1], "sprg-fallback") == 0) {
        host->cpu.pc = 0x500u;
        host->cpu.gpr[4] = 0xB6B6B6B6u;
        host->cpu.instruction_fallback(&host->cpu, 0x7C9043A6u, host->cpu.pc);
        assert(host->error == NULL && host->cpu.pc == 0x504u);
        host->cpu.instruction_fallback(&host->cpu, 0x7C7042A6u, host->cpu.pc);
        assert(host->error == NULL && host->cpu.pc == 0x508u && host->cpu.gpr[3] == 0xB6B6B6B6u);
    } else if (strcmp(argv[1], "sprg-priv-read") == 0 || strcmp(argv[1], "sprg-priv-write") == 0) {
        host->cpu.msr = PPC_MSR_PR;
        if (strcmp(argv[1], "sprg-priv-read") == 0)
            ppc_mfspr(&host->cpu, 272u, 0x500u);
        else
            ppc_mtspr(&host->cpu, 272u, 0xB6B6B6B6u, 0x500u);
        assert(host->error == NULL && host->cpu.program_exception == PPC_PROGRAM_PRIV);
        host->cpu.msr = 0u;
        assert(ppc_mfspr(&host->cpu, 272u, 0x500u) == 0u && host->error == NULL);
    } else if (strcmp(argv[1], "unknown-write") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00300Cu, 1u, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00300Cu);
    } else if (strncmp(argv[1], "physical-read-", 14u) == 0 || strncmp(argv[1], "physical-write-", 15u) == 0) {
        const bool write = argv[1][9] == 'w';
        const u8 size = (u8)strtoul(argv[1] + (write ? 15u : 14u), NULL, 10);
        assert(size == 1u || size == 2u || size == 4u || size == 8u);
        host->cpu.msr = 0u;
        const u64 value = 0x1122334455667788ull;
        if (write) {
            memset(host->cpu.ram + 0xB8u, 0xA5u, 24u);
            host->cpu.external_write(&host->cpu, 0xC0u, value, size);
            const u64 mask = size == 8u ? UINT64_MAX : (1ull << (size * 8u)) - 1u;
            assert(host->cpu.external_read(&host->cpu, 0xC0u, size) == (value & mask));
            assert(host->cpu.ram[0xBFu] == 0xA5u && host->cpu.ram[0xC0u + size] == 0xA5u);
        } else {
            write_be64(host->cpu.ram + 0xC0u, value);
            assert(host->cpu.external_read(&host->cpu, 0xC0u, size) == (value >> (64u - size * 8u)));
        }
        assert(host->error == NULL && host->cycles.absolute_cycles == 0u && host->cpu.timebase == 0u);
    } else if (strcmp(argv[1], "physical-translated") == 0) {
        host->cpu.msr = PPC_MSR_DR;
        host->cpu.external_read(&host->cpu, 0xC0u, 4u);
        assert(host->error != NULL && host->error_address == 0xC0u);
    } else if (strcmp(argv[1], "physical-alias") == 0 || strcmp(argv[1], "physical-partial-alias") == 0) {
        const u8 alias[4] = {1u, 2u, 3u, 4u};
        const u32 offset = strcmp(argv[1], "physical-alias") == 0 ? 0u : 2u;
        assert(ppc_guest_alias_add(0x800000C0u + offset, sizeof(alias), alias));
        host->cpu.msr = 0u;
        host->cpu.external_read(&host->cpu, 0xC0u, 4u);
        assert(host->error != NULL && host->error_address == 0xC0u);
        assert(read_be32(host->cpu.ram + 0xC0u) == 0u);
    } else if (strncmp(argv[1], "cp-control-", 11u) == 0) {
        const DolInterrupts before = host->interrupts;
        host->cpu.pc = 0x80321308u;
        const bool credit = strcmp(argv[1], "cp-control-credit") == 0;
        const bool store = strcmp(argv[1], "cp-control-store") == 0;
        const bool bad_value = strncmp(argv[1], "cp-control-value-", 17u) == 0;
        const bool bad_width = strncmp(argv[1], "cp-control-width-", 17u) == 0;
        const u64 value = bad_value ? strtoul(argv[1] + 17u, NULL, 0) : 0u;
        const u8 width = bad_width ? (u8)strtoul(argv[1] + 17u, NULL, 0) : 2u;
        if (credit) {
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            host->cpu.downcount -= 256;
        }
        if (store) {
            host->cpu.gpr[3] = 0xCC000000u;
            host->cpu.gpr[0] = 0u;
            mem_write16(&host->cpu, host->cpu.gpr[3] + 2u, (u16)host->cpu.gpr[0]);
        } else {
            host->cpu.external_write(&host->cpu, 0xCC000002u, value, width);
        }
        if (bad_value || bad_width) {
            assert(host->error != NULL && host->error_address == 0xCC000002u);
            assert(host->error_pc == 0x80321308u && host->error_width == width && host->error_value == value);
        } else {
            assert(host->error == NULL && host->cpu.exception == 0u);
            host->cpu.external_write(&host->cpu, 0xCC000002u, 0u, 2u);
            assert(host->error == NULL);
        }
        assert(memcmp(&before, &host->interrupts, sizeof(before)) == 0);
        assert(host->frontier.size == 0u && !host->frontier.complete);
        assert(host->cpu.pc == 0x80321308u);
        assert(host->cycles.absolute_cycles == (credit ? 256u : 0u));
        if (credit) {
            assert(host->cpu.cycle_budget == 0 && host->cpu.timebase == 21u);
            assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
        }
    } else if (strcmp(argv[1], "pi-cold-reset") == 0) {
        const DolInterrupts before = host->interrupts;
        host->cpu.pc = 0x803069E8u;
        assert(host->cpu.external_read(&host->cpu, 0xCC003024u, 4u) == 0u);
        assert(host->cpu.external_read(&host->cpu, 0xCC003024u, 4u) == 0u);
        assert(host->error == NULL && host->cpu.exception == 0u);
        assert(memcmp(&before, &host->interrupts, sizeof(before)) == 0);
        assert(host->cycles.absolute_cycles == 0u && host->cpu.timebase == 0u);
    } else if (strcmp(argv[1], "pi-reset-load") == 0) {
        host->cpu.pc = 0x803069E8u;
        host->cpu.gpr[3] = 0xCC003000u;
        host->cpu.gpr[0] = 0xFFFFFFFFu;
        host->cpu.gpr[0] = mem_read32(&host->cpu, host->cpu.gpr[3] + 36u);
        assert(host->error == NULL && host->cpu.gpr[0] == 0u);
        assert(host->cpu.pc == 0x803069E8u && host->cpu.exception == 0u);
    } else if (strcmp(argv[1], "pi-reset-write") == 0) {
        host->cpu.pc = 0x803069E8u;
        host->cpu.external_write(&host->cpu, 0xCC003024u, 4u, 4u);
        assert(host->error != NULL && host->error_address == 0xCC003024u);
        assert(host->error_pc == 0x803069E8u && host->error_width == 4u && host->error_value == 4u);
        assert(host->cpu.external_read(&host->cpu, 0xCC003024u, 4u) == 0u);
    } else if (strncmp(argv[1], "pi-reset-width-", 15u) == 0) {
        const u8 size = (u8)strtoul(argv[1] + 15u, NULL, 10);
        assert(size == 1u || size == 2u || size == 8u);
        host->cpu.pc = 0x803069E8u;
        host->cpu.external_read(&host->cpu, 0xCC003024u, size);
        assert(host->error != NULL && host->error_address == 0xCC003024u);
        assert(host->error_pc == 0x803069E8u);
    } else if (strcmp(argv[1], "pi-reset-credit") == 0) {
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount -= 256;
        host->cpu.external_read(&host->cpu, 0xCC003024u, 4u);
        assert(host->error == NULL && host->cycles.absolute_cycles == 256u);
        assert(host->cpu.timebase == 21u && host->cpu.cycle_budget == 0);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
    } else if (strcmp(argv[1], "mi-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00401Cu, 0x15u, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC00401Cu, 2u) == 0x15u);
    } else if (strcmp(argv[1], "mi-init-reset") == 0) {
        assert(host->cpu.external_read(&host->cpu, 0xCC004020u, 2u) == 0u);
        assert(host->cpu.external_read(&host->cpu, 0xCC004010u, 2u) == 0u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "mi-init-zero") == 0 || strcmp(argv[1], "mi-init-unrestricted") == 0) {
        const bool unrestricted = strcmp(argv[1], "mi-init-unrestricted") == 0;
        const u32 address = unrestricted ? 0xCC004010u : 0xCC004020u;
        const u16 value = unrestricted ? 0xFFu : 0u;
        host->cpu.external_write(&host->cpu, address, value, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, address, 2u) == value);
        assert(host->cpu.external_read(&host->cpu, 0xCC00401Cu, 2u) == 0u);
        assert(!dol_interrupts_external_pending(&host->interrupts));
        assert(host->cpu.timebase == 0u && host->cycles.absolute_cycles == 0u);
    } else if (strncmp(argv[1], "mi-init-bad-", 12u) == 0) {
        const bool protection = strcmp(argv[1], "mi-init-bad-protection") == 0 ||
                                strcmp(argv[1], "mi-init-bad-protection-width") == 0;
        const bool width = strstr(argv[1], "-width") != NULL;
        const u32 address = protection ? 0xCC004010u : 0xCC004020u;
        const u16 value = width ? 0u : protection ? 0xFEu : 1u;
        host->cpu.pc = 0x80305DA4u;
        host->cpu.external_write(&host->cpu, address, value, width ? 4u : 2u);
        assert(host->error != NULL && host->error_address == address && host->error_pc == host->cpu.pc);
        assert(host->error_width == (width ? 4u : 2u) && host->error_value == value);
        assert(host->cpu.external_read(&host->cpu, address, 2u) == 0u);
    } else if (strcmp(argv[1], "mi-invalid-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00401Cu, 0x20u, 2u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00401Cu);
    } else if (strcmp(argv[1], "mi-width") == 0) {
        host->cpu.external_read(&host->cpu, 0xCC00401Cu, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00401Cu);
    } else if (strncmp(argv[1], "dsp-fixed-", 10u) == 0) {
        for (u32 index = 0u; index < 128u; ++index)
            host->cpu.ram[0x01000000u + index] = (u8)index;
        if (strncmp(argv[1], "dsp-fixed-altered-", 18u) == 0) {
            const u32 index = (u32)strtoul(argv[1] + 18u, NULL, 10);
            assert(index < 128u);
            host->cpu.ram[0x01000000u + index] ^= 1u;
            host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x0805u, 2u);
            assert(host->error != NULL && host->error_address == 0xCC00500Au);
            assert(host->error_width == 2u && host->error_value == 0x0805u);
            assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0804u);
        } else {
            const u64 cycles = host->cycles.absolute_cycles;
            const u64 tb = host->cpu.timebase;
            host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x08ADu, 2u);
            assert(host->error == NULL);
            assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0804u);
            assert(host->cpu.external_read(&host->cpu, 0xCC005004u, 2u) == 0u);
            assert(host->cpu.external_read(&host->cpu, 0xCC005006u, 2u) == 0u);
            if (strcmp(argv[1], "dsp-fixed-reset") == 0) {
                assert(host->cycles.absolute_cycles == cycles && host->cpu.timebase == tb);
                assert(read_be32(host->audio.ai_regs) == DOL_AUDIO_DMA_AI_CONTROL_INIT);
                assert(!dol_interrupts_external_pending(&host->interrupts));
            } else if (strcmp(argv[1], "dsp-fixed-rom-mail") == 0) {
                charge_cycles(host, 12u);
                host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x0800u, 2u);
                assert(host->error == NULL && host->cpu.timebase == tb + 1u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005004u, 2u) == 0x8071u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005004u, 2u) == 0x8071u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005006u, 2u) == 0xFEEDu);
                assert(host->cpu.external_read(&host->cpu, 0xCC005004u, 2u) == 0x0071u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005006u, 2u) == 0xFEEDu);
                assert(!dol_interrupts_external_pending(&host->interrupts));
            } else if (strcmp(argv[1], "dsp-fixed-halted-cpu-mail") == 0) {
                host->cpu.external_write(&host->cpu, 0xCC005000u, 0x0040u, 2u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005000u, 2u) == 0x0040u);
                host->cpu.external_write(&host->cpu, 0xCC005002u, 0x1234u, 2u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005000u, 2u) == 0x8040u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005002u, 2u) == 0x1234u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005000u, 2u) == 0x0040u);
                assert(host->error == NULL);
            } else if (strcmp(argv[1], "dsp-fixed-running-cpu-mail") == 0) {
                host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x0800u, 2u);
                host->cpu.external_write(&host->cpu, 0xCC005002u, 0x1234u, 2u);
                assert(host->error != NULL && host->error_address == 0xCC005002u);
                assert(host->error_width == 2u && host->error_value == 0x1234u);
            } else if (strcmp(argv[1], "dsp-fixed-queued-cpu-mail") == 0) {
                host->cpu.external_write(&host->cpu, 0xCC005002u, 0x1234u, 2u);
                host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x0800u, 2u);
                assert(host->error != NULL && host->error_address == 0xCC00500Au);
                assert(host->cpu.external_read(&host->cpu, 0xCC005004u, 2u) == 0u);
            } else if (strcmp(argv[1], "dsp-fixed-changed-after-reset") == 0) {
                host->cpu.ram[0x0100007Fu] ^= 1u;
                host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x0800u, 2u);
                assert(host->error != NULL && host->error_width == 2u);
                assert(host->cpu.external_read(&host->cpu, 0xCC005004u, 2u) == 0u);
            } else {
                assert(!"unknown fixed DSP scenario");
            }
        }
    } else if (strcmp(argv[1], "dsp-stopped") == 0) {
        assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0804u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "dsp-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x0954u, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0954u);
    } else if (strcmp(argv[1], "dsp-ack") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00500Au, 0x09FCu, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0954u);
    } else if (strcmp(argv[1], "dsp-reset") == 0 || strcmp(argv[1], "dsp-release") == 0) {
        const u16 value = strcmp(argv[1], "dsp-reset") == 0 ? 0x0805u : 0x0004u;
        host->cpu.external_write(&host->cpu, 0xCC00500Au, value, 2u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00500Au);
        assert(host->cpu.external_read(&host->cpu, 0xCC00500Au, 2u) == 0x0804u);
    } else if (strcmp(argv[1], "dsp-width") == 0) {
        host->cpu.external_read(&host->cpu, 0xCC00500Au, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00500Au);
    } else if (strcmp(argv[1], "ai-stopped") == 0) {
        assert(host->cpu.external_read(&host->cpu, 0xCC006C00u, 4u) == 0x42u);
        assert(host->error == NULL);
    } else if (strcmp(argv[1], "ai-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC006C00u, 0x46u, 4u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC006C00u, 4u) == 0x46u);
    } else if (strcmp(argv[1], "ai-ack") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC006C00u, 0x4Eu, 4u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC006C00u, 4u) == 0x46u);
    } else if (strcmp(argv[1], "ai-reset-counter") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC006C00u, 0x62u, 4u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC006C00u, 4u) == 0x42u);
    } else if (strcmp(argv[1], "ai-start") == 0 || strcmp(argv[1], "ai-rate") == 0) {
        const u32 value = strcmp(argv[1], "ai-start") == 0 ? 0x43u : 0x02u;
        host->cpu.external_write(&host->cpu, 0xCC006C00u, value, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC006C00u);
    } else if (strcmp(argv[1], "ai-width") == 0) {
        host->cpu.external_read(&host->cpu, 0xCC006C00u, 2u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC006C00u);
    } else if (strncmp(argv[1], "si-empty-", 9u) == 0 && argv[1][9] >= '0' && argv[1][9] <= '3') {
        const u32 channel = (u32)strtoul(argv[1] + 9u, NULL, 10);
        assert(channel < 4u);
        host->cpu.pc = 0x8030C1C0u;
        host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_IO_BUFFER_OFF, 0x00ABCDEFu, 4u);
        host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF,
                                 0xC0010301u | (channel << 1u), 4u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 4u) ==
               (0xE0010300u | (channel << 1u)));
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + DOL_SI_STATUS_OFF, 4u) ==
               (0x08000000u >> (channel * 8u)));
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + DOL_SI_IO_BUFFER_OFF, 4u) == 0x00ABCDEFu);
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + channel * 12u + 4u, 4u) == 0u);
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + channel * 12u + 8u, 4u) == 0u);
        assert(dol_si_interrupt_pending(&host->si));
        assert(host->interrupts.pi_cause & DOL_PI_CAUSE_SI);
    } else if (strcmp(argv[1], "si-empty-ack") == 0) {
        host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010301u, 4u);
        assert(host->error == NULL);
        host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
        assert(!dol_si_interrupt_pending(&host->si));
        assert(!(host->interrupts.pi_cause & DOL_PI_CAUSE_SI));
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 4u) == 0x60010300u);
        host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_STATUS_OFF, 0x08000000u, 4u);
        assert(host->cpu.external_read(&host->cpu, DOL_SI_BASE + DOL_SI_STATUS_OFF, 4u) == 0u);
    } else if (strncmp(argv[1], "irq-", 4u) == 0) {
        const bool di = strcmp(argv[1], "irq-di") == 0;
        const bool masked = strcmp(argv[1], "irq-masked") == 0;
        const bool disabled = strcmp(argv[1], "irq-disabled") == 0;
        const bool acknowledge = strcmp(argv[1], "irq-ack-rfi") == 0;
        const bool prior_exception = strcmp(argv[1], "irq-prior-exception") == 0;
        const u32 cause = di ? DOL_PI_CAUSE_DI : DOL_PI_CAUSE_SI;
        host->cpu.pc = 0x8030C1C4u;
        const u32 msr = PPC_MSR_EE | PPC_MSR_FP | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_RI;
        host->cpu.msr = disabled ? msr & ~PPC_MSR_EE : msr;
        host->cpu.external_write(&host->cpu, DOL_PI_INTERRUPT_MASK, masked ? 0u : cause, 4u);
        if (di) {
            host->di.control = DOL_DI_CONTROL_TSTART;
            host->di.status = DOL_DI_STATUS_DEINTMASK;
            dol_di_complete_command(&host->di, DOL_DI_COMMAND_ERROR);
        } else {
            host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010301u, 4u);
        }
        assert(host->error == NULL);
        if (prior_exception)
            host->cpu.exception = PPC_EXC_PROGRAM;
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount = -120;
        const bool delivered = ww_host_edge(host, &host->cpu, host->cpu.pc);
        assert(host->error == NULL);
        assert(!host->edge_stopped);
        if (masked || disabled || prior_exception) {
            assert(!delivered);
            assert(host->cpu.pc == 0x8030C1C4u);
            assert(host->cpu.srr0 == 0u);
            assert(host->interrupts.pi_cause & cause);
            assert(host->cpu.exception == (prior_exception ? PPC_EXC_PROGRAM : 0u));
            if (disabled) {
                host->cpu.msr |= PPC_MSR_EE;
                assert(ww_host_edge(host, &host->cpu, host->cpu.pc));
                assert(host->cpu.srr0 == 0x8030C1C4u);
            }
        } else {
            assert(delivered);
            assert(host->cpu.pc == 0x500u);
            assert(host->cpu.srr0 == 0x8030C1C4u);
            assert(host->cpu.srr1 == msr);
            assert(host->cpu.msr == 0u);
            assert(host->cpu.exception == 0u);
            assert(host->cycles.absolute_cycles == 120u);
            assert(!ww_host_edge(host, &host->cpu, host->cpu.pc));
            if (acknowledge)
                host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
            ppc_rfi(&host->cpu, 0x80000540u);
            assert(host->cpu.pc == 0x8030C1C4u);
            assert(host->cpu.msr == msr);
            assert(ww_host_edge(host, &host->cpu, host->cpu.pc) == !acknowledge);
        }
    } else if (strcmp(argv[1], "si-width") == 0) {
        host->cpu.external_write(&host->cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 1u, 2u);
        assert(host->error != NULL);
        assert(dol_si_mmio_read(&host->si, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 4u) == 0u);
    } else if (strcmp(argv[1], "module") == 0) {
        StaticRecompModuleDesc module = {.abi_version = 99u, .dispatch = dispatch};
        BlueWakeRelData data = {0};
        assert(!ww_host_load(host, &module, &data, 0u, "not-opened.dol"));
        assert(strstr(host->error, "ABI") != NULL);
        assert(host->alias_count == 0u);
    } else if (strncmp(argv[1], "exi-trace-", 10u) == 0) {
        const bool payload = strcmp(argv[1], "exi-trace-error-payload") == 0;
        const bool late = strcmp(argv[1], "exi-trace-error-late") == 0;
        const bool fail = payload || late || strcmp(argv[1], "exi-trace-error-first") == 0;
        fail_exi_log = payload ? 2u : late ? 9u : fail ? 1u : 0u;
        const u32 transfers = payload || (fail && !late) ? 1u : late ? 1000u : 1001u;
        for (u32 transfer = 1u; transfer <= transfers; ++transfer) {
            host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, DOL_EXI_STATUS_TCINT, 4u);
            exi_sram_command(host, 0x20000100u);
            if (fail && !payload && transfer == transfers)
                break;
            assert(host->error == NULL && host->sram.command_latched);
        }
        if (payload) {
            host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, 0x138u, 4u);
            host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 0x31u, 4u);
            assert(host->sram.command_latched && host->sram.cursor == 0u);
            assert(host->exi.channels[0].immediate_data == 0x20000100u);
        }
        if (fail) {
            assert(host->error != NULL && strcmp(host->error, "EXI transfer trace write failed") == 0);
            assert(host->error_address == DOL_EXI_BASE + DOL_EXI_CONTROL_OFF);
            assert(host->error_pc == 0x80309E1Cu);
            assert(host->sram.command_latched == payload);
            assert(!(host->exi.channels[0].status & DOL_EXI_STATUS_TCINT));
            assert(host->exi.channels[0].control & DOL_EXI_CONTROL_TSTART);
        } else {
            assert(exi_logs == 9u);
        }
    } else if (strcmp(argv[1], "exi-context") == 0) {
        exi_sram_command(host, 0x20000100u);
        assert(host->error == NULL);
        assert(!(host->exi.channels[0].control & DOL_EXI_CONTROL_TSTART));
        assert(host->exi.channels[0].status & DOL_EXI_STATUS_TCINT);
        assert(!(host->exi.channels[0].status & DOL_EXI_STATUS_EXT));
    } else if (strcmp(argv[1], "exi-sram-dma") == 0) {
        memset(host->cpu.ram + 0x1000u, 0xA5u, 128u);
        exi_sram_command(host, 0x20000100u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, 0x13Cu, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_DMA_ADDRESS_OFF, 0x1020u, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_DMA_LENGTH_OFF, 64u, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 3u, 4u);
        assert(host->error == NULL);
        assert(mem_read32(&host->cpu, 0x80001020u) == 0x002CFFD0u);
        assert(mem_read8(&host->cpu, 0x80001020u + 19u) == 0x2Cu);
        assert(mem_read32(&host->cpu, 0x8000101Cu) == 0xA5A5A5A5u);
        assert(mem_read8(&host->cpu, 0x80001020u + 63u) == 0u);
        assert(mem_read32(&host->cpu, 0x80001060u) == 0xA5A5A5A5u);
        assert(dol_exi_interrupt_pending(&host->exi));
        assert(host->interrupts.pi_cause & DOL_PI_CAUSE_EXI);
    } else if (strcmp(argv[1], "exi-sram-immediate") == 0 || strcmp(argv[1], "exi-sram-reselect") == 0) {
        exi_sram_command(host, 0x20000100u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 0x31u, 4u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF, 4u) == 0x002CFFD0u);
        if (strcmp(argv[1], "exi-sram-reselect") == 0) {
            host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, 0u, 4u);
            exi_sram_command(host, 0x20000100u);
            host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 0x31u, 4u);
            assert(host->error == NULL);
            assert(host->cpu.external_read(&host->cpu, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF, 4u) == 0x002CFFD0u);
        }
    } else if (strcmp(argv[1], "exi-sram-write") == 0) {
        exi_sram_command(host, 0xA00005C0u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF, 0x28000000u, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 5u, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, 0u, 4u);
        exi_sram_command(host, 0x200005C0u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 1u, 4u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF, 4u) == 0x28000000u);
    } else if (strcmp(argv[1], "exi-rom") == 0) {
        exi_sram_command(host, 0u);
        assert(host->error != NULL);
        assert(host->exi.channels[0].control & DOL_EXI_CONTROL_TSTART);
    } else if (strcmp(argv[1], "exi-bad-dma") == 0) {
        exi_sram_command(host, 0x20000100u);
        assert(host->error == NULL);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_DMA_ADDRESS_OFF, 0x01800000u, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_DMA_LENGTH_OFF, 64u, 4u);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 3u, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0x01800000u);
    } else if (strcmp(argv[1], "exi-bad-range") == 0) {
        exi_sram_command(host, 0x200010C0u);
        assert(host->error == NULL);
        host->cpu.external_write(&host->cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 0x31u, 4u);
        assert(host->error != NULL);
    } else if (strncmp(argv[1], "data-cache-", 11u) == 0) {
        const u32 xo = (u32)strtoul(argv[1] + 11u, NULL, 10);
        assert(xo == 54u || xo == 86u || xo == 470u);
        memset(host->cpu.ram + 0x1000u, 0xA5u, 96u);
        host->cpu.gpr[0] = 0xCC007000u;
        host->cpu.gpr[3] = 0x80001025u;
        host->cpu.gpr[4] = 3u;
        host->cpu.pc = 0x803030C8u;
        const u32 ra = strstr(argv[1], "indexed") != NULL ? 4u : 0u;
        host->cpu.instruction_fallback(&host->cpu,
            (31u << 26u) | (ra << 16u) | (3u << 11u) | (xo << 1u), host->cpu.pc);
        assert(host->error == NULL);
        assert(host->cpu.exception == 0u);
        assert(host->cpu.pc == 0x803030CCu);
        for (u32 offset = 0u; offset < 96u; ++offset)
            assert(host->cpu.ram[0x1000u + offset] == 0xA5u);
    } else if (strcmp(argv[1], "dcbi-privilege") == 0) {
        host->cpu.msr |= PPC_MSR_PR;
        host->cpu.gpr[3] = 0x80001025u;
        host->cpu.instruction_fallback(&host->cpu, 0x7C001BACu, 0x803030C8u);
        assert(host->error == NULL);
        assert(host->cpu.exception == PPC_EXC_PROGRAM);
        assert(host->cpu.program_exception & PPC_PROGRAM_PRIV);
        assert(host->cpu.srr0 == 0x803030C8u);
    } else if (strncmp(argv[1], "icbi-", 5u) == 0) {
        synthetic_dol();
        StaticRecompModuleDesc module = synthetic_module();
        BlueWakeRelData data = {0};
        assert(ww_host_load(host, &module, &data, 0u, "synthetic.dol"));
        const bool modified = strcmp(argv[1], "icbi-modified-text") == 0;
        host->cpu.pc = 0x803031D8u;
        host->cpu.gpr[3] = strcmp(argv[1], "icbi-data") == 0 ? 0x80002004u :
                            strcmp(argv[1], "icbi-text-physical") == 0 ? 0x1000u :
                            strcmp(argv[1], "icbi-text-uncached") == 0 ? 0xC0001000u : 0x80001000u;
        if (modified)
            mem_write32(&host->cpu, 0x80001000u, 0x12345678u);
        host->cpu.instruction_fallback(&host->cpu, 0x7C001FACu, host->cpu.pc);
        if (modified) {
            assert(host->error != NULL);
            assert(host->cpu.pc == 0x803031D8u);
            assert(host->error_address == 0x80001000u);
            assert(mem_read32(&host->cpu, 0x80001000u) == 0x12345678u);
        } else {
            assert(host->error == NULL);
            assert(host->cpu.pc == 0x803031DCu);
        }
    } else if (strncmp(argv[1], "guest-offer-", 12u) == 0 || strcmp(argv[1], "unknown-host-call") == 0) {
        synthetic_dol();
        StaticRecompModuleDesc module = synthetic_module();
        BlueWakeRelData data = {0};
        assert(ww_host_load(host, &module, &data, 0u, "synthetic.dol"));
        const u32 address = strcmp(argv[1], "guest-offer-physical") == 0 ? 0x1000u :
                            strcmp(argv[1], "guest-offer-uncached") == 0 ? 0xC0001000u : 0x80001000u;
        if (strcmp(argv[1], "unknown-host-call") == 0) {
            assert(ppc_host_call(&host->cpu, PPC_HOST_CALL_NATIVE_REGION_QUERY));
            assert(host->error != NULL);
            assert(host->error_address == PPC_HOST_CALL_NATIVE_REGION_QUERY);
        } else {
            assert(!ppc_host_call(&host->cpu, address));
            assert(host->cpu.pc == module.entry_point);
            assert(host->error == NULL);
            assert(host->cpu.exception == 0u);
        }
    } else if (strcmp(argv[1], "aliases") == 0 || strcmp(argv[1], "alias-failure") == 0 ||
               strcmp(argv[1], "code-copy-failure") == 0) {
        synthetic_dol();
        StaticRecompModuleDesc module = synthetic_module();
        static const u8 bytes[] = {1u, 2u, 3u, 4u};
        BlueWakeRelData data = {.module_id = 1u, .section_index = 3u,
                               .linked_start = 0xC0402000u, .size = 4u, .bytes = bytes};
        if (strcmp(argv[1], "code-copy-failure") == 0) {
            fail_allocation = allocation + 2u;
            assert(!ww_host_load(host, &module, &data, 1u, "synthetic.dol"));
            assert(strstr(host->error, "snapshot") != NULL);
            assert(host->code_guard.bytes == 0u);
            assert(host->code_guard.text[0] == NULL);
            assert(host->code_guard.text[1] == NULL);
        } else if (strcmp(argv[1], "alias-failure") == 0) {
            fail_allocation = allocation + 3u;
            assert(!ww_host_load(host, &module, &data, 1u, "synthetic.dol"));
            assert(strstr(host->error, "REL alias") != NULL);
            assert(host->code_guard.bytes == 8u);
            assert(host->alias_count == 0u);
        } else {
            assert(ww_host_load(host, &module, &data, 1u, "synthetic.dol"));
            assert(host->alias_count == 2u);
            assert(host->alias_bytes == 12u);
            assert(mem_read32(&host->cpu, 0xC0402000u) == 0x01020304u);
            assert(mem_read32(&host->cpu, 0xC0403000u) == 0u);
            u8* storage;
            assert(!ppc_guest_alias_get_storage(0x81500000u, 256u, &storage));
            assert(!ppc_guest_alias_get_storage(0xC0401000u, 256u, &storage));
            assert(host->cpu.pc == module.entry_point);
        }
    } else {
        return 2;
    }
    ww_host_shutdown(host);
    assert(host->code_guard.bytes == 0u);
    for (u32 index = 0u; index < DOL_NUM_TEXT; ++index)
        assert(host->code_guard.text[index] == NULL);
    free(host);
    return 0;
}
