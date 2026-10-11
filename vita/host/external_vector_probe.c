#include "probe_support.h"
#include "external_vector.h"
#include <stdlib.h>
#include <string.h>

static int unsupported_dispatch(CPUState* cpu, u32 address) {
    (void)cpu;
    (void)address;
    return 0;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    StaticRecompRange range = {0x80001000u, 0x80001004u};
    StaticRecompModuleDesc module = {.dispatch = unsupported_dispatch, .code_ranges = &range, .num_code_ranges = 1u};
    host->module = &module;
    CPUState* cpu = &host->cpu;
    memcpy(cpu->ram + WW05_EXTERNAL_VECTOR, ww05_external_vector_reference, WW05_EXTERNAL_VECTOR_BYTES);
    mem_write32(cpu, 0x800000C0u, 0x80002000u);
    mem_write32(cpu, 0x80003010u, 0x80004000u);
    mem_write32(cpu, 0x8000200Cu, 0xCCCCCCCCu);
    cpu->pc = WW05_EXTERNAL_VECTOR;
    cpu->srr0 = 0x80001234u;
    cpu->srr1 = PPC_MSR_EE | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_RI;
    cpu->gpr[3] = 0xA5A5A5A5u;
    cpu->gpr[4] = 0xB6B6B6B6u;
    const bool corrupt = strncmp(argv[1], "corrupt-", 8u) == 0;
    const bool unaligned = strcmp(argv[1], "unaligned") == 0;
    const bool translated = strcmp(argv[1], "translated") == 0;
    const bool pending = strcmp(argv[1], "pending") == 0;
    const bool short_ram = strcmp(argv[1], "short-ram") == 0;
    if (corrupt) {
        const u32 offset = (u32)strtoul(argv[1] + 8u, NULL, 10);
        assert(offset < WW05_EXTERNAL_VECTOR_BYTES);
        cpu->ram[WW05_EXTERNAL_VECTOR + offset] ^= 1u;
    }
    if (unaligned)
        ++cpu->pc;
    if (translated)
        cpu->msr = PPC_MSR_IR;
    if (pending)
        cpu->exception = PPC_EXC_SYSTEM_CALL;
    const u32 ram_size = cpu->ram_size;
    if (short_ram)
        cpu->ram_size = WW05_EXTERNAL_VECTOR + WW05_EXTERNAL_VECTOR_BYTES - 1u;
    const bool zero = strcmp(argv[1], "zero-budget") == 0;
    const bool single = strcmp(argv[1], "single-cycle") == 0;
    if (single)
        host->cycles.cap = 1;
    u32 dispatches = 0u;
    do {
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        if (single)
            cpu->cycle_deadline_budget = 1;
        if (zero)
            cpu->cycle_budget = 0;
        const u32 address = cpu->pc;
        const int dispatched = ww_host_dispatch(host, address);
        bluewake_cycle_domain_end_turn(&host->cycles, cpu);
        if (corrupt || unaligned || translated || pending || short_ram) {
            assert(dispatched == 0 && host->error != NULL);
            assert(host->error_address == address && host->error_pc == address);
            assert(cpu->pc == address && cpu->gpr[3] == 0xA5A5A5A5u && cpu->gpr[4] == 0xB6B6B6B6u);
            assert(host->cycles.absolute_cycles == 0u);
            cpu->ram_size = ram_size;
            assert(mem_read32(cpu, 0x8000200Cu) == 0xCCCCCCCCu);
            break;
        }
        assert(dispatched == 1 && host->error == NULL && ++dispatches < 64u);
        if (zero) {
            assert(cpu->pc == WW05_EXTERNAL_VECTOR && cpu->gpr[4] == 0xB6B6B6B6u);
            assert(host->cycles.absolute_cycles == 0u);
            assert(mem_read32(cpu, 0x8000200Cu) == 0xCCCCCCCCu);
            break;
        }
    } while (cpu->pc >= WW05_EXTERNAL_VECTOR && cpu->pc < WW05_EXTERNAL_VECTOR + WW05_EXTERNAL_VECTOR_BYTES);
    if (!host->error && !zero) {
        assert(strcmp(argv[1], "execute") == 0 || single);
        assert(cpu->pc == 0x80004000u && cpu->msr == (PPC_MSR_IR | PPC_MSR_DR));
        assert(cpu->gpr[3] == 4u && cpu->gpr[4] == 0x80002000u);
        assert(mem_read32(cpu, 0x8000200Cu) == 0xA5A5A5A5u);
        assert(mem_read32(cpu, 0x80002010u) == 0xB6B6B6B6u);
        assert(mem_read32(cpu, 0x80002198u) == 0x80001234u);
        assert(mem_read32(cpu, 0x8000219Cu) == (PPC_MSR_EE | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_RI));
        assert(host->cycles.absolute_cycles != 0u && cpu->exception == 0u);
        assert(single ? dispatches > 3u : dispatches == 3u);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
