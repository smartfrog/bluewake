#include "boot_host.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void* __real_calloc(size_t count, size_t size);
static unsigned allocation;
static unsigned fail_allocation;
void* __wrap_calloc(size_t count, size_t size) {
    if (++allocation == fail_allocation)
        return NULL;
    return __real_calloc(count, size);
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
    } else if (strcmp(argv[1], "unknown-write") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00300Cu, 1u, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00300Cu);
    } else if (strcmp(argv[1], "mi-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00401Cu, 0x15u, 2u);
        assert(host->error == NULL);
        assert(host->cpu.external_read(&host->cpu, 0xCC00401Cu, 2u) == 0x15u);
    } else if (strcmp(argv[1], "mi-invalid-mask") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC00401Cu, 0x20u, 2u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00401Cu);
    } else if (strcmp(argv[1], "mi-width") == 0) {
        host->cpu.external_read(&host->cpu, 0xCC00401Cu, 4u);
        assert(host->error != NULL);
        assert(host->error_address == 0xCC00401Cu);
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
