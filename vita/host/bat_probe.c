#include "probe_support.h"
#include <stdlib.h>
#include <string.h>

static void prepare_bank(WwHost* host, u16 base) {
    ppc_mtspr(&host->cpu, base, 0u, 0x80305C4Cu);
    ppc_mtspr(&host->cpu, base + 1u, 2u, 0x80305C50u);
    ppc_mtspr(&host->cpu, base, 0x800001FFu, 0x80305C54u);
    ppc_mtspr(&host->cpu, base + 4u, 0u, 0x80305C6Cu);
    ppc_mtspr(&host->cpu, base + 5u, 0x01000002u, 0x80305C70u);
    ppc_mtspr(&host->cpu, base + 4u, 0x810000FFu, 0x80305C74u);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    host->cpu.pc = 0x80305C4Cu;
    host->cpu.msr = 0u;
    if (strcmp(argv[1], "boot") == 0) {
        assert(ppc_mfspr(&host->cpu, 528u, host->cpu.pc) == 0x80001FFFu);
        assert(ppc_mfspr(&host->cpu, 529u, host->cpu.pc) == 2u);
        assert(ppc_mfspr(&host->cpu, 536u, host->cpu.pc) == 0x80001FFFu);
        assert(ppc_mfspr(&host->cpu, 537u, host->cpu.pc) == 2u);
        assert(ppc_mfspr(&host->cpu, 538u, host->cpu.pc) == 0xC0001FFFu);
        assert(ppc_mfspr(&host->cpu, 539u, host->cpu.pc) == 0x2Au);
        assert(ppc_mfspr(&host->cpu, 532u, host->cpu.pc) == 0u);
        assert(host->error == NULL && host->cycles.absolute_cycles == 0u);
    } else if (strcmp(argv[1], "fallback") == 0) {
        host->cpu.gpr[7] = 0u;
        host->cpu.instruction_fallback(&host->cpu, 0x7CF883A6u, host->cpu.pc);
        assert(host->error == NULL && host->cpu.pc == 0x80305C50u);
        host->cpu.gpr[4] = 2u;
        host->cpu.instruction_fallback(&host->cpu, 0x7C9983A6u, host->cpu.pc);
        assert(host->error == NULL && host->cpu.pc == 0x80305C54u);
        host->cpu.gpr[3] = 0x800001FFu;
        host->cpu.instruction_fallback(&host->cpu, 0x7C7883A6u, host->cpu.pc);
        assert(host->error == NULL && host->cpu.pc == 0x80305C58u);
        assert(ppc_mfspr(&host->cpu, 536u, host->cpu.pc) == 0x800001FFu);
    } else if (strcmp(argv[1], "user-write") == 0 || strcmp(argv[1], "user-read") == 0) {
        host->cpu.msr = PPC_MSR_PR;
        if (strcmp(argv[1], "user-write") == 0)
            ppc_mtspr(&host->cpu, 536u, 0u, host->cpu.pc);
        else
            ppc_mfspr(&host->cpu, 536u, host->cpu.pc);
        assert(host->error == NULL && host->cpu.program_exception == PPC_PROGRAM_PRIV);
        host->cpu.msr = 0u;
        assert(ppc_mfspr(&host->cpu, 536u, host->cpu.pc) == 0x80001FFFu);
    } else if (strcmp(argv[1], "active-write") == 0) {
        host->cpu.msr = PPC_MSR_DR;
        ppc_mtspr(&host->cpu, 536u, 0u, host->cpu.pc);
        assert(host->error != NULL);
        assert(ppc_mfspr(&host->cpu, 536u, host->cpu.pc) == 0x80001FFFu);
    } else if (strcmp(argv[1], "lower-active") == 0) {
        ppc_mtspr(&host->cpu, 537u, 0u, host->cpu.pc);
        assert(host->error != NULL);
        assert(ppc_mfspr(&host->cpu, 537u, host->cpu.pc) == 2u);
    } else if (strcmp(argv[1], "unknown-base") == 0 || strcmp(argv[1], "unknown-size") == 0 ||
               strcmp(argv[1], "unknown-physical") == 0 || strcmp(argv[1], "upper-before-lower") == 0) {
        ppc_mtspr(&host->cpu, 540u, 0u, host->cpu.pc);
        const bool physical = strcmp(argv[1], "unknown-physical") == 0;
        const u32 value = physical ? 0x00200002u : strcmp(argv[1], "unknown-base") == 0 ? 0x900000FFu :
                          strcmp(argv[1], "unknown-size") == 0 ? 0x810001FFu : 0x810000FFu;
        ppc_mtspr(&host->cpu, physical ? 541u : 540u, value, host->cpu.pc);
        assert(host->error != NULL && host->error_pc == 0x80305C4Cu);
        assert(host->error_address == (physical ? 541u : 540u));
        assert(host->error_width == 4u && host->error_value == value);
        assert(ppc_mfspr(&host->cpu, physical ? 541u : 540u, host->cpu.pc) == 0u);
    } else {
        prepare_bank(host, 536u);
        assert(host->error == NULL);
        if (strcmp(argv[1], "incomplete") == 0) {
            ppc_mtspr(&host->cpu, 540u, 0u, host->cpu.pc);
            host->cpu.msr = PPC_MSR_DR;
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            host->cpu.downcount -= 12;
            assert(ww_host_edge(host, &host->cpu, 0x80305CA0u));
            assert(host->error != NULL);
            assert(host->cycles.absolute_cycles == 12u && host->cpu.timebase == 1u);
        } else {
            prepare_bank(host, 528u);
            assert(host->error == NULL);
            if (strcmp(argv[1], "rfi") == 0) {
                host->cpu.srr0 = 0x803018E4u;
                host->cpu.srr1 = PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_RI;
                ppc_rfi(&host->cpu, 0x80305CA0u);
                assert(host->cpu.pc == 0x803018E4u);
                assert(!ww_host_edge(host, &host->cpu, host->cpu.pc));
                write_be32(host->cpu.ram + 0x01000000u, 0x12345678u);
                assert(mem_read32(&host->cpu, 0x81000000u) == 0x12345678u);
                assert(mem_read32(&host->cpu, 0xC1000000u) == 0x12345678u);
                mem_write32(&host->cpu, 0x80001000u, 0x89ABCDEFu);
                assert(read_be32(host->cpu.ram + 0x1000u) == 0x89ABCDEFu);
            } else {
                assert(strcmp(argv[1], "readback") == 0);
                for (u16 base = 528u; base <= 536u; base += 8u) {
                    assert(ppc_mfspr(&host->cpu, base, host->cpu.pc) == 0x800001FFu);
                    assert(ppc_mfspr(&host->cpu, base + 1u, host->cpu.pc) == 2u);
                    assert(ppc_mfspr(&host->cpu, base + 4u, host->cpu.pc) == 0x810000FFu);
                    assert(ppc_mfspr(&host->cpu, base + 5u, host->cpu.pc) == 0x01000002u);
                }
                assert(host->cycles.absolute_cycles == 0u && host->cpu.timebase == 0u);
            }
        }
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
