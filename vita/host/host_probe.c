#include "boot_host.h"
#include "probe_support.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void* __real_calloc(size_t count, size_t size);
static unsigned allocation;
static unsigned fail_allocation;
static u32 exi_logs;
static u32 fail_exi_log;
static char pointer_line[160];
static unsigned pointer_lines;
static char lever_line[200];
static unsigned lever_lines;
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
    if (stream == stderr && strncmp(format, "[host-ptr]", 10u) == 0) {
        va_list copy;
        va_copy(copy, arguments);
        vsnprintf(pointer_line, sizeof(pointer_line), format, copy);
        va_end(copy);
        ++pointer_lines;
    }
    if (stream == stderr && strncmp(format, "[idle-lever]", 12u) == 0) {
        va_list copy;
        va_copy(copy, arguments);
        vsnprintf(lever_line, sizeof(lever_line), format, copy);
        va_end(copy);
        ++lever_lines;
    }
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
    const char* fifo_scenario;
    const WwFifoProbeRegister* fifo_probe = ww_probe_fifo_register(argv[1], &fifo_scenario);
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
        assert(ww_gx_write(&host->frontier, &host->gx, 0x61u, 1u, host->cpu.pc, host->cpu.gpr[1]));
        assert(!ww_host_edge(host, &host->cpu, host->cpu.pc));
        assert(ww_gx_write(&host->frontier, &host->gx, 0xFEFFFFFFu, 4u, host->cpu.pc, host->cpu.gpr[1]));
        assert(ww_host_edge(host, &host->cpu, host->cpu.pc));
        assert(host->edge_stopped);
        assert(host->frontier.complete);
    } else if (strcmp(argv[1], "instruction") == 0) {
        host->cpu.pc = 0x80001000u;
        host->cpu.instruction_fallback(&host->cpu, 0xFFFFFFFFu, host->cpu.pc);
        assert(host->cpu.pc == 0x80001000u);
        assert(host->error != NULL);
    } else if (strncmp(argv[1], "vrbox2-edge-", 12u) == 0) {
        const bool owned = strcmp(argv[1], "vrbox2-edge-owned") == 0;
        const bool unowned = strcmp(argv[1], "vrbox2-edge-unowned") == 0;
        const bool alias = strcmp(argv[1], "vrbox2-edge-alias") == 0;
        const bool mismatch = strcmp(argv[1], "vrbox2-edge-mismatch") == 0;
        const bool unsupported = strcmp(argv[1], "vrbox2-edge-unsupported") == 0;
        assert(owned || unowned || alias || mismatch || unsupported);
        const u32 address = unsupported ? 0x802305E0u : 0x8015EA5Cu;
        StaticRecompRange range = {address, address + 4u};
        StaticRecompModuleDesc module = {.dispatch = dispatch, .code_ranges = &range, .num_code_ranges = 1u};
        host->module = unowned ? NULL : &module;
        host->cpu.pc = mismatch ? address + 4u : address;
        host->cpu.lr = 0x800190A0u;
        host->cpu.gpr[3] = 0x80410000u;
        host->cpu.srr0 = 0x802AE070u;
        host->cpu.srr1 = 0x0000A032u;
        if (alias)
            assert(ppc_guest_alias_add(address, 4u, NULL));
        bool admitted = false;
#if WW_EDGE_OBSERVATION_ADMIT
        admitted = owned;
#endif
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        CPUState before = host->cpu;
        const DolInterrupts interrupts_before = host->interrupts;
        assert(ww_host_edge(host, &host->cpu, address) == !admitted);
        assert((host->error == NULL) == admitted && host->edge_stopped == !admitted);
        if (!admitted)
            before.exception |= PPC_EXC_PROGRAM;
        assert(memcmp(&host->cpu, &before, sizeof before) == 0);
        assert(host->cycles.absolute_cycles == 0u);
        assert(memcmp(&host->interrupts, &interrupts_before, sizeof interrupts_before) == 0);
        if (!admitted) {
            assert(strcmp(host->error, "desktop interception unsupported; guest not skipped") == 0);
            assert(host->error_address == address && host->error_pc == before.pc);
        }
    } else if (strncmp(argv[1], "epilogue-edge-", 14u) == 0) {
        const bool retrace = strncmp(argv[1], "epilogue-edge-retrace", 21u) == 0;
        const bool receive = strncmp(argv[1], "epilogue-edge-receive", 21u) == 0;
        const bool context = strcmp(argv[1], "epilogue-edge-context") == 0 ||
                             strcmp(argv[1], "epilogue-edge-context-unowned") == 0;
        const bool compiled = strcmp(argv[1], "epilogue-edge-compiled") == 0 ||
                              strcmp(argv[1], "epilogue-edge-context") == 0 ||
                              strcmp(argv[1], "epilogue-edge-retrace") == 0 ||
                              strcmp(argv[1], "epilogue-edge-receive") == 0;
        const bool unowned = strcmp(argv[1], "epilogue-edge-unowned") == 0 ||
                             strcmp(argv[1], "epilogue-edge-context-unowned") == 0 ||
                             strcmp(argv[1], "epilogue-edge-retrace-unowned") == 0 ||
                             strcmp(argv[1], "epilogue-edge-receive-unowned") == 0;
        const bool service = strcmp(argv[1], "epilogue-edge-service") == 0 ||
                             strcmp(argv[1], "epilogue-edge-retrace-bad-caller") == 0 ||
                             strcmp(argv[1], "epilogue-edge-retrace-bad-queue") == 0 ||
                             strcmp(argv[1], "epilogue-edge-retrace-bad-sda") == 0 ||
                             strcmp(argv[1], "epilogue-edge-retrace-alias") == 0;
        const bool receive_negative = strcmp(argv[1], "epilogue-edge-receive-bad-address") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-bad-caller") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-bad-queue") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-bad-sda") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-alias") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-unmapped") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-unbacked") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-waiting") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-head") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-tail") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-used") == 0 ||
                                      strcmp(argv[1], "epilogue-edge-receive-unowned-lr") == 0;
        assert(compiled || unowned || service || receive_negative);
        StaticRecompRange range = retrace ? (StaticRecompRange){0x80308A9Cu, 0x80308AA0u} :
                                  strcmp(argv[1], "epilogue-edge-receive-unowned-lr") == 0 ? (StaticRecompRange){0x80308A9Cu, 0x80308AA0u} :
                                  strcmp(argv[1], "epilogue-edge-receive-bad-address") == 0 ? (StaticRecompRange){0x803056E0u, 0x80322B24u} :
                                  receive ? (StaticRecompRange){0x803056E0u, 0x803096E0u} :
                                  service ? (StaticRecompRange){0x803193ACu, 0x803193B0u} :
                                  context ? (StaticRecompRange){0x80303A50u, 0x80303B28u} :
                                            (StaticRecompRange){0x80328F84u, 0x80328F9Cu};
        StaticRecompModuleDesc module = {.dispatch = dispatch, .code_ranges = &range, .num_code_ranges = 1u};
        host->module = unowned ? NULL : &module;
        host->cpu.pc = strcmp(argv[1], "epilogue-edge-receive-bad-address") == 0 ? 0x80322B20u :
                       receive ? 0x80308A9Cu : range.start;
        host->cpu.lr = 0x8030141Cu;
        host->cpu.gpr[11] = 0x8040CF60u;
        host->cpu.gpr[3] = 0x803A2960u;
        if (retrace) {
            host->cpu.lr = strcmp(argv[1], "epilogue-edge-retrace-bad-caller") == 0 ? 0x80322C38u : 0x80313A2Cu;
            host->cpu.gpr[3] = strcmp(argv[1], "epilogue-edge-retrace-bad-queue") == 0 ? 0x803F7C9Cu : 0x803F7B44u;
            host->cpu.gpr[13] = strcmp(argv[1], "epilogue-edge-retrace-bad-sda") == 0 ? 0x8040E0E0u : 0x803FE0E0u;
            if (strcmp(argv[1], "epilogue-edge-retrace-alias") == 0)
                assert(ppc_guest_alias_add(0x803F7B44u, 8u, NULL));
        }
        if (receive) {
            host->cpu.lr = strcmp(argv[1], "epilogue-edge-receive-bad-caller") == 0 ? 0x80322C38u : 0x80305A28u;
            host->cpu.gpr[3] = strcmp(argv[1], "epilogue-edge-receive-bad-queue") == 0 ? 0x803F7C9Cu :
                               strcmp(argv[1], "epilogue-edge-receive-unmapped") == 0 ? 0x818A72C8u : 0x803A72C8u;
            host->cpu.gpr[13] = strcmp(argv[1], "epilogue-edge-receive-bad-sda") == 0 ? 0x8040E0E0u : 0x803FE0E0u;
            if (strcmp(argv[1], "epilogue-edge-receive-alias") == 0)
                assert(ppc_guest_alias_add(0x803A72C8u, 8u, NULL));
            if (strcmp(argv[1], "epilogue-edge-receive-waiting") == 0 || strcmp(argv[1], "epilogue-edge-receive-head") == 0)
                mem_write32(&host->cpu, 0x803A72C8u, 0x803A5F90u);
            if (strcmp(argv[1], "epilogue-edge-receive-waiting") == 0 || strcmp(argv[1], "epilogue-edge-receive-tail") == 0)
                mem_write32(&host->cpu, 0x803A72CCu, 0x803A5F90u);
            if (strcmp(argv[1], "epilogue-edge-receive-used") == 0)
                mem_write32(&host->cpu, 0x803A72DCu, 1u);
        }
        host->cpu.srr0 = 0x802AE070u;
        host->cpu.srr1 = 0x0000A032u;
        for (u32 reg = 27u; reg <= 31u; ++reg)
            host->cpu.gpr[reg] = 0xA5000000u | reg;
        const CPUState before = host->cpu;
        const DolInterrupts interrupts_before = host->interrupts;
        const u32 queue_before = mem_read32(&host->cpu, 0x803F7B44u);
        u8 receive_queue_before[0x20];
        memcpy(receive_queue_before, host->cpu.ram + 0x003A72C0u, sizeof receive_queue_before);
        const u32 ram_size = host->cpu.ram_size;
        const u32 expected_ram_size = strcmp(argv[1], "epilogue-edge-receive-unbacked") == 0 ? 0x003A72DFu : ram_size;
        host->cpu.ram_size = expected_ram_size;
        assert(ww_host_edge(host, &host->cpu, host->cpu.pc) == !compiled);
        assert((host->error == NULL) == compiled && host->edge_stopped == !compiled);
        assert(host->cpu.ram_size == expected_ram_size);
        host->cpu.ram_size = ram_size;
        assert(host->cpu.pc == before.pc && host->cpu.lr == before.lr && host->cpu.msr == before.msr);
        assert(host->cpu.srr0 == before.srr0 && host->cpu.srr1 == before.srr1);
        assert(memcmp(host->cpu.gpr, before.gpr, sizeof(before.gpr)) == 0);
        assert(memcmp(host->cpu.gqr, before.gqr, sizeof(before.gqr)) == 0);
        assert(memcmp(host->cpu.fpr, before.fpr, sizeof(before.fpr)) == 0 && host->cpu.fpscr == before.fpscr);
        assert(host->cycles.absolute_cycles == 0u);
        assert(memcmp(&interrupts_before, &host->interrupts, sizeof(interrupts_before)) == 0);
        assert(mem_read32(&host->cpu, 0x803F7B44u) == queue_before);
        assert(memcmp(host->cpu.ram + 0x003A72C0u, receive_queue_before, sizeof receive_queue_before) == 0);
        if (!compiled) {
            assert(host->error_address == before.pc && host->error_pc == before.pc);
            if (receive)
                assert(strcmp(host->error, "desktop interception unsupported; guest not skipped") == 0);
        }
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
        host->cpu.external_write(&host->cpu, 0xCC003008u, 1u, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC003008u);
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
    } else if (fifo_probe != NULL) {
        const DolInterrupts before = host->interrupts;
        const char* scenario = fifo_scenario;
        const u32 address = fifo_probe->address;
        const u32 pc = fifo_probe->pc;
        const u8 supported_width = fifo_probe->width;
        const u32 mask = fifo_probe->mask;
        host->cpu.pc = pc;
        const bool reset = strcmp(scenario, "reset") == 0;
        const bool store = strcmp(scenario, "store") == 0;
        const bool credit = strcmp(scenario, "credit") == 0;
        const bool bad_write = strncmp(scenario, "write-width-", 12u) == 0;
        const bool bad_read = strncmp(scenario, "read-width-", 11u) == 0;
        const bool masked = strncmp(scenario, "mask-", 5u) == 0;
        const u64 value = masked ? strtoul(scenario + 5u, NULL, 0) : fifo_probe->value;
        const u8 width = bad_write ? (u8)strtoul(scenario + 12u, NULL, 0) :
                         bad_read ? (u8)strtoul(scenario + 11u, NULL, 0) : supported_width;
        if (credit) {
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            host->cpu.downcount -= 256;
        }
        if (store) {
            host->cpu.gpr[3] = address & 0xFFFFF000u;
            host->cpu.gpr[0] = (u32)value;
            const u32 offset = address & 0xFFFu;
            if (supported_width == 2u)
                mem_write16(&host->cpu, host->cpu.gpr[3] + offset, (u16)host->cpu.gpr[0]);
            else
                mem_write32(&host->cpu, host->cpu.gpr[3] + offset, host->cpu.gpr[0]);
        } else if (!reset) {
            host->cpu.external_write(&host->cpu, address, value, bad_write ? width : supported_width);
        }
        if (bad_read) {
            assert(host->error == NULL);
            host->cpu.external_read(&host->cpu, address, width);
        }
        if (bad_write || bad_read) {
            assert(host->error != NULL && host->error_address == address && host->error_pc == host->cpu.pc);
            if (bad_write)
                assert(host->error_width == width && host->error_value == value);
            assert(host->cpu.external_read(&host->cpu, address, supported_width) == (bad_write ? 0u : value & mask));
        } else {
            assert(host->error == NULL && host->cpu.exception == 0u);
            assert(host->cpu.external_read(&host->cpu, address, supported_width) == (reset ? 0u : value & mask));
        }
        assert(memcmp(&before, &host->interrupts, sizeof(before)) == 0);
        assert(host->frontier.size == 0u && !host->frontier.complete);
        assert(host->cpu.pc == pc);
        assert(host->cycles.absolute_cycles == (credit ? 256u : 0u));
        if (credit) {
            assert(host->cpu.cycle_budget == 0 && host->cpu.timebase == 21u);
            assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
        }
    } else if (strcmp(argv[1], "pi-fifo-independent") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00300Cu, 0x00429F60u, 4u);
        host->cpu.external_write(&host->cpu, 0xCC003010u, 0x004C9F5Cu, 4u);
        host->cpu.external_write(&host->cpu, 0xCC003014u, 0x00429FA0u, 4u);
        host->cpu.external_write(&host->cpu, 0xCC000020u, 0xABDFu, 2u);
        host->cpu.external_write(&host->cpu, 0xCC000024u, 0xCDEFu, 2u);
        host->cpu.external_write(&host->cpu, 0xCC000034u, 0xEF5Fu, 2u);
        host->cpu.external_write(&host->cpu, 0xCC000038u, 0xA19Fu, 2u);
        host->cpu.external_write(&host->cpu, 0xCC000028u, 0x321Fu, 2u);
        host->cpu.external_write(&host->cpu, 0xCC00002Au, 9u, 2u);
        host->cpu.external_write(&host->cpu, 0xCC00002Cu, 0x543Fu, 2u);
        host->cpu.external_write(&host->cpu, 0xCC00002Eu, 5u, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC00300Cu, 4u) == 0x00429F60u);
        assert(host->cpu.external_read(&host->cpu, 0xCC003010u, 4u) == 0x004C9F40u);
        assert(host->cpu.external_read(&host->cpu, 0xCC003014u, 4u) == 0x00429FA0u);
        assert(host->cpu.external_read(&host->cpu, 0xCC000020u, 2u) == 0xABC0u);
        assert(host->cpu.external_read(&host->cpu, 0xCC000024u, 2u) == 0xCDE0u);
        assert(host->cpu.external_read(&host->cpu, 0xCC000034u, 2u) == 0xEF40u);
        assert(host->cpu.external_read(&host->cpu, 0xCC000038u, 2u) == 0xA180u);
        assert(host->cpu.external_read(&host->cpu, 0xCC000028u, 2u) == 0x3200u);
        assert(host->cpu.external_read(&host->cpu, 0xCC00002Au, 2u) == 9u);
        assert(host->cp_fifo_high_watermark == 0x00093200u);
        assert(host->cpu.external_read(&host->cpu, 0xCC00002Cu, 2u) == 0x5420u);
        assert(host->cpu.external_read(&host->cpu, 0xCC00002Eu, 2u) == 5u);
        assert(host->cp_fifo_low_watermark == 0x00055420u);
    } else if (strncmp(argv[1], "cp-base-halves-", 15u) == 0 || strncmp(argv[1], "cp-end-halves-", 14u) == 0 ||
               strncmp(argv[1], "cp-wptr-halves-", 15u) == 0 || strncmp(argv[1], "cp-rptr-halves-", 15u) == 0 ||
               strncmp(argv[1], "cp-hiwater-halves-", 18u) == 0 || strncmp(argv[1], "cp-lowater-halves-", 18u) == 0) {
        const bool end = strncmp(argv[1], "cp-end-halves-", 14u) == 0;
        const bool wptr = strncmp(argv[1], "cp-wptr-halves-", 15u) == 0;
        const bool rptr = strncmp(argv[1], "cp-rptr-halves-", 15u) == 0;
        const bool hiwater = strncmp(argv[1], "cp-hiwater-halves-", 18u) == 0;
        const bool lowater = strncmp(argv[1], "cp-lowater-halves-", 18u) == 0;
        const char* order = argv[1] + (hiwater || lowater ? 18u : end ? 14u : 15u);
        assert(strcmp(order, "low-first") == 0 || strcmp(order, "high-first") == 0);
        const bool high_first = strcmp(order, "high-first") == 0;
        const u32 low_address = hiwater ? 0xCC000028u : lowater ? 0xCC00002Cu : end ? 0xCC000024u :
                                wptr ? 0xCC000034u : rptr ? 0xCC000038u : 0xCC000020u;
        const u32 high_address = low_address + 2u;
        const u32 high_value = hiwater ? 9u : lowater ? 5u : end ? 0x4Cu : 0x42u;
        const u32 low_value = hiwater ? 0xC000u : lowater ? 0u : end ? 0x9F5Cu : 0x9F60u;
        const u32 masked_low = low_value & 0xFFE0u;
        u32* full_value = hiwater ? &host->cp_fifo_high_watermark : lowater ? &host->cp_fifo_low_watermark :
                          end ? &host->cp_fifo_end :
                          wptr ? &host->cp_fifo_write_pointer : rptr ? &host->cp_fifo_read_pointer : &host->cp_fifo_base;
        const DolInterrupts before = host->interrupts;
        host->cpu.external_write(&host->cpu, high_first ? high_address : low_address, high_first ? high_value : low_value, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, high_first ? low_address : high_address, 2u) == 0u);
        host->cpu.external_write(&host->cpu, high_first ? low_address : high_address, high_first ? low_value : high_value, 2u);
        assert(host->error == NULL && *full_value == ((high_value << 16u) | masked_low));
        assert(host->cpu.external_read(&host->cpu, low_address, 2u) == masked_low);
        assert(host->cpu.external_read(&host->cpu, high_address, 2u) == high_value);
        host->cpu.external_write(&host->cpu, high_address, UINT16_MAX, 2u);
        assert(host->error == NULL && *full_value == (0x03FF0000u | masked_low));
        assert(host->cpu.external_read(&host->cpu, low_address, 2u) == masked_low);
        host->cpu.external_write(&host->cpu, low_address, 0xABDFu, 2u);
        assert(host->error == NULL && *full_value == 0x03FFABC0u);
        assert(host->cpu.external_read(&host->cpu, high_address, 2u) == 0x03FFu);
        assert(memcmp(&before, &host->interrupts, sizeof(before)) == 0);
        assert(host->frontier.size == 0u && !host->frontier.complete && host->cycles.absolute_cycles == 0u);
    } else if (strcmp(argv[1], "cp-rptr-high-sync-read") == 0 || strcmp(argv[1], "cp-rptr-high-sync-write") == 0) {
        host->cpu.pc = 0x80321018u;
        host->cpu.external_write(&host->cpu, 0xCC000038u, 0x9F60u, 2u);
        assert(host->error == NULL);
        assert(ww_gx_write(&host->frontier, &host->gx, 0x10u, 1u, host->cpu.pc - 4u, host->cpu.gpr[1]));
        const WwGxCheckpoint before = host->frontier;
        const DolInterrupts interrupts = host->interrupts;
        if (strcmp(argv[1], "cp-rptr-high-sync-read") == 0) {
            host->cpu.external_read(&host->cpu, 0xCC00003Au, 2u);
        } else {
            host->cpu.external_write(&host->cpu, 0xCC00003Au, 0x42u, 2u);
            assert(host->error_width == 2u && host->error_value == 0x42u);
        }
        assert(host->error != NULL && host->error_address == 0xCC00003Au && host->error_pc == 0x80321018u);
        assert(strcmp(host->error, "CP read pointer sync requires empty disabled init; FIFO processor backend not implemented") == 0);
        assert(host->cp_fifo_read_pointer == 0x9F60u);
        assert(memcmp(&before, &host->frontier, sizeof(before)) == 0);
        assert(memcmp(&interrupts, &host->interrupts, sizeof(interrupts)) == 0);
        assert(host->cycles.absolute_cycles == 0u && host->cpu.pc == 0x80321018u);
    } else if (strncmp(argv[1], "cp-control-", 11u) == 0 || strncmp(argv[1], "cp-distance-low-", 16u) == 0 ||
               strncmp(argv[1], "cp-distance-high-", 17u) == 0) {
        const DolInterrupts before = host->interrupts;
        const bool high = strncmp(argv[1], "cp-distance-high-", 17u) == 0;
        const bool distance = high || strncmp(argv[1], "cp-distance-low-", 16u) == 0;
        const char* scenario = argv[1] + (high ? 17u : distance ? 16u : 11u);
        const u32 address = high ? 0xCC000032u : distance ? 0xCC000030u : 0xCC000002u;
        const u32 pc = high ? 0x80320FF8u : distance ? 0x80320F98u : 0x80321308u;
        host->cpu.pc = pc;
        const bool credit = strcmp(scenario, "credit") == 0 || strcmp(scenario, "mask-credit") == 0;
        const bool store = strcmp(scenario, "store") == 0 || strcmp(scenario, "mask-store") == 0;
        const bool masked = !distance && strncmp(scenario, "mask", 4u) == 0;
        const bool clear_mask = strcmp(scenario, "mask-clear") == 0;
        const bool reset = strcmp(scenario, "reset") == 0;
        const bool queued = strcmp(scenario, "queued") == 0;
        const bool read = strcmp(scenario, "read") == 0;
        const bool bad_value = strncmp(scenario, "value-", 6u) == 0;
        const bool bad_width = strncmp(scenario, "width-", 6u) == 0;
        const bool bad_read = strncmp(scenario, "read-width-", 11u) == 0;
        const u64 value = bad_value ? strtoul(scenario + 6u, NULL, 0) : masked ? 4u : 0u;
        const u8 width = bad_width ? (u8)strtoul(scenario + 6u, NULL, 0) :
                         bad_read ? (u8)strtoul(scenario + 11u, NULL, 0) : 2u;
        if (!distance && (bad_value || bad_width || bad_read)) {
            host->cpu.external_write(&host->cpu, address, 4u, 2u);
            assert(host->error == NULL);
        }
        if (queued) {
            assert(distance);
            assert(ww_gx_write(&host->frontier, &host->gx, 0x10u, 1u, pc - 4u, host->cpu.gpr[1]));
            assert(host->frontier.size == 1u && !host->frontier.complete);
        }
        const WwGxCheckpoint frontier = host->frontier;
        if (credit) {
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            host->cpu.downcount -= 256;
        }
        if (reset) {
            assert(!distance && host->cpu.external_read(&host->cpu, address, 2u) == 0u);
        } else if (store) {
            host->cpu.gpr[3] = 0xCC000000u;
            host->cpu.gpr[0] = (u32)value;
            mem_write16(&host->cpu, host->cpu.gpr[3] + (high ? 50u : distance ? 48u : 2u), (u16)host->cpu.gpr[0]);
        } else if (read || bad_read) {
            assert(distance || bad_read);
            host->cpu.external_read(&host->cpu, address, width);
        } else {
            host->cpu.external_write(&host->cpu, address, value, width);
        }
        if (bad_value || bad_width || bad_read || queued || read) {
            assert(host->error != NULL && host->error_address == address && host->error_pc == pc);
            if (!read && !bad_read)
                assert(host->error_width == width && host->error_value == value);
            if (!distance)
                assert(host->cpu.external_read(&host->cpu, address, 2u) == 4u);
        } else {
            assert(host->error == NULL && host->cpu.exception == 0u);
            host->cpu.external_write(&host->cpu, address, value, 2u);
            assert(host->error == NULL);
            if (!distance)
                assert(host->cpu.external_read(&host->cpu, address, 2u) == value);
            if (clear_mask) {
                host->cpu.external_write(&host->cpu, address, 0u, 2u);
                assert(host->error == NULL && host->cpu.external_read(&host->cpu, address, 2u) == 0u);
            }
        }
        assert(memcmp(&before, &host->interrupts, sizeof(before)) == 0);
        assert(memcmp(&frontier, &host->frontier, sizeof(frontier)) == 0);
        assert(host->cpu.pc == pc);
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
    } else if (strcmp(argv[1], "idle-lever-alias") == 0) {
        // The first runtime REL scratch alias sits past the real 24 MiB MEM1
        // end but below the coarse 0x90000000 end, so it raises the coarse
        // "aliases overlap MEM1" flag the emitted poll predicate refuses on.
        // The idle lever must still fire: the poll word itself is unshadowed.
        assert(ppc_guest_alias_add(0x81822E80u, 16500u, NULL));
        assert(ww_host_idle_poll_stable(&host->cpu));
        // The guard must still bite when the word really is shadowed.
        assert(ppc_guest_alias_add(0x803F7A30u, 4u, NULL));
        assert(!ww_host_idle_poll_stable(&host->cpu));
        ppc_guest_alias_clear();
        assert(ww_host_idle_poll_stable(&host->cpu));
    } else if (strcmp(argv[1], "host-ptr-reload") == 0) {
        // A dispatch that loses the loop's pointer is repaired from the startup
        // pointer without dereferencing the stale one, and a null anchor only
        // reports zeros. The report is one-shot; recovery continues.
        host->cpu.pc = 0x80307EF4u;
        host->cpu.gpr[1] = 0x81234560u;
        WwHost* stale = host;
        assert(!ww_host_pointer_reload(&stale, NULL));
        assert(stale == host);
        assert(pointer_lines == 1u);
        assert(strstr(pointer_line, "[host-ptr] restored=") != NULL);
        assert(strstr(pointer_line, "before=") != NULL);
        assert(strstr(pointer_line, "pc=00000000") != NULL);
        assert(strstr(pointer_line, "sp=00000000") != NULL);
        stale = NULL;
        assert(ww_host_pointer_reload(&stale, host));
        assert(stale == host);
        assert(pointer_lines == 1u);
        stale = NULL;
        assert(ww_host_pointer_reload(&stale, host));
        assert(stale == host);
        assert(pointer_lines == 1u);
        assert(!ww_host_pointer_reload(&stale, host));
        assert(pointer_lines == 1u);
        // Shutdown reports the lever line; only the two real restores count.
        assert(!ww_host_idle_poll_ready(host));
        ww_host_shutdown(host);
        assert(lever_lines == 1u);
        assert(strstr(lever_line, "ptr_restores=2") != NULL);
        free(host);
        return 0;
    } else if (strcmp(argv[1], "host-null") == 0) {
        // The observed crash class: a null host reaches the loop path. The
        // guard must print the durable record once and abort, never continue.
        int fds[2];
        assert(pipe(fds) == 0);
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            close(fds[0]);
            const int redirected = dup2(fds[1], STDERR_FILENO);
            (void)redirected;
            close(fds[1]);
            ww_host_end_dispatch(NULL);
            _exit(0);
        }
        close(fds[1]);
        char buffer[512] = {0};
        const ssize_t got = read(fds[0], buffer, sizeof(buffer) - 1u);
        close(fds[0]);
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(got > 0);
        assert(strstr(buffer, "[host-null] where=loop") != NULL);
        assert(strstr(buffer, "last_jump_turn=") != NULL);
        assert(strstr(buffer, "last_src=") != NULL);
        assert(strstr(buffer, "last_dist=") != NULL);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        free(host);
        return 0;
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
