#include "probe_support.h"
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    const u32 destination = 0x2000u;
    const bool rejected = strncmp(argv[1], "reject-", 7u) == 0;
    u32 address = destination;
    u32 length = 32u;
    u32 control = DOL_DI_CONTROL_TSTART | DOL_DI_CONTROL_DMA;
    u32 opcode = 0x12000000u;
    if (strcmp(argv[1], "reject-no-dma") == 0)
        control = DOL_DI_CONTROL_TSTART;
    if (strcmp(argv[1], "reject-write") == 0)
        control |= DOL_DI_CONTROL_WRITE;
    if (strcmp(argv[1], "reject-empty") == 0)
        length = 0u;
    if (strcmp(argv[1], "reject-long") == 0)
        length = 64u;
    if (strcmp(argv[1], "reject-range") == 0)
        address = cpu->ram_size;
    if (strcmp(argv[1], "reject-subcommand") == 0)
        opcode |= 1u;
    if (strcmp(argv[1], "reject-alias") == 0) {
        const u8 alias[4] = {1u, 2u, 3u, 4u};
        assert(ppc_guest_alias_add(GC_RAM_BASE + destination + 8u, sizeof(alias), alias));
    }
    memset(cpu->ram + destination - 32u, 0xA5u, 128u);
    cpu->pc = 0x8030EF44u;
    cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_DI, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_STATUS_OFF, DOL_DI_STATUS_TCINTMASK, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_COMMAND_0_OFF, opcode, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_COMMAND_1_OFF, 0x12345678u, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_COMMAND_2_OFF, 32u, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_DMA_ADDRESS_OFF, address, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_DMA_LENGTH_OFF, length, 4u);
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
    cpu->downcount = -40;
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_CONTROL_OFF, control, 4u);
    bluewake_cycle_domain_end_turn(&host->cycles, cpu);
    assert(host->cycles.absolute_cycles == 40u && cpu->timebase == 3u);
    assert(cpu->pc == 0x8030EF44u);
    if (rejected) {
        assert(host->error != NULL && host->error_pc == 0x8030EF44u && host->error_address == opcode);
        assert(host->di.status & DOL_DI_STATUS_DEINT);
        assert(!(host->di.status & DOL_DI_STATUS_TCINT));
        assert(host->di.dma_address == address && host->di.dma_length == length);
        for (u32 offset = 0u; offset < 128u; ++offset)
            assert(cpu->ram[destination - 32u + offset] == 0xA5u);
    } else {
        assert(strcmp(argv[1], "response") == 0 || strcmp(argv[1], "irq") == 0);
        assert(host->error == NULL && cpu->exception == 0u);
        assert(read_be32(cpu->ram + destination) == 0x00000002u);
        assert(read_be32(cpu->ram + destination + 4u) == 0x20060526u);
        assert(read_be32(cpu->ram + destination + 8u) == 0x41000000u);
        for (u32 offset = 0u; offset < 32u; ++offset)
            assert(cpu->ram[destination - 32u + offset] == 0xA5u);
        for (u32 offset = 12u; offset < 96u; ++offset)
            assert(cpu->ram[destination + offset] == 0xA5u);
        assert(host->di.status & DOL_DI_STATUS_TCINT);
        assert(!(host->di.status & DOL_DI_STATUS_DEINT));
        assert(host->di.dma_address == destination + 32u && host->di.dma_length == 0u);
        assert(!(host->di.control & DOL_DI_CONTROL_TSTART));
        if (strcmp(argv[1], "irq") == 0) {
            assert(!ww_host_edge(host, cpu, cpu->pc) && cpu->pc == 0x8030EF44u);
            cpu->msr |= PPC_MSR_EE;
            assert(ww_host_edge(host, cpu, cpu->pc));
            assert(cpu->pc == 0x500u && cpu->srr0 == 0x8030EF44u);
            cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_STATUS_OFF,
                                DOL_DI_STATUS_TCINTMASK | DOL_DI_STATUS_TCINT, 4u);
            assert(!dol_di_interrupt_pending(&host->di));
        }
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
