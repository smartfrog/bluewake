#include "probe_support.h"
#include "gxruntime/dvd.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[di-read]", 9u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static void read_command(WwHost* host, u32 opcode, u32 physical, u32 offset, u32 length, u32 control) {
    CPUState* cpu = &host->cpu;
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_COMMAND_0_OFF, opcode, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_COMMAND_1_OFF, offset / 4u, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_COMMAND_2_OFF, length, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_DMA_ADDRESS_OFF, physical, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_DMA_LENGTH_OFF, length, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_CONTROL_OFF, control, 4u);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    const char* scenario = argv[1];
    CPUState* cpu = &host->cpu;
    cpu->pc = 0x80001000u;
    u32 opcode = 0xA8000000u;
    u32 physical = 0x2000u;
    u32 offset = 0x500u;
    u32 length = 32u;
    u32 control = DOL_DI_CONTROL_TSTART | DOL_DI_CONTROL_DMA;
    if (strcmp(scenario, "no-dma") == 0)
        control = DOL_DI_CONTROL_TSTART;
    else if (strcmp(scenario, "write") == 0)
        control |= DOL_DI_CONTROL_WRITE;
    else if (strcmp(scenario, "range") == 0)
        physical = cpu->ram_size;
    else if (strcmp(scenario, "empty") == 0)
        length = 0u;
    else if (strcmp(scenario, "subcommand") == 0)
        opcode |= 1u;
    else if (strcmp(scenario, "alias") == 0)
        assert(ppc_guest_alias_add(GC_RAM_BASE + physical + 12u, 4u, NULL));
    else if (strcmp(scenario, "alias-late") == 0) {
        length = 512u;
        assert(ppc_guest_alias_add(GC_RAM_BASE + physical + 300u, 4u, NULL));
    }
    else if (strcmp(scenario, "short-read") == 0) {
        offset = 0x1000u - 32u;
        length = 64u;
    } else if (strcmp(scenario, "trace") == 0)
        fail_trace = true;
    memset(cpu->ram + 0x1FE0u, 0xA5u, 128u);
    const WwGatherPipe gather = host->gather;
    const WwGxCheckpoint frontier = host->frontier;
    cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_DI, 4u);
    cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_STATUS_OFF, DOL_DI_STATUS_TCINTMASK, 4u);
    read_command(host, opcode, physical, offset, length, control);
    if (strcmp(scenario, "response") == 0 || strcmp(scenario, "irq") == 0 || strcmp(scenario, "io-failure") == 0) {
        assert(host->error == NULL && cpu->exception == 0u && dvd_image_ready());
        for (u32 index = 0u; index < 32u; ++index)
            assert(cpu->ram[physical + index] == (u8)((offset + index) * 7u + 3u));
        assert(host->di.dma_address == physical + 32u && host->di.dma_length == 0u);
        assert(host->di.status & DOL_DI_STATUS_TCINT);
        assert(!(host->di.status & DOL_DI_STATUS_DEINT));
        assert(dvd_path_to_entrynum("boot.bin") == 1);
        if (strcmp(scenario, "irq") == 0) {
            assert(!ww_host_edge(host, cpu, cpu->pc));
            cpu->msr |= PPC_MSR_EE;
            assert(ww_host_edge(host, cpu, cpu->pc));
            assert(cpu->pc == 0x500u && cpu->srr0 == 0x80001000u);
        } else if (strcmp(scenario, "io-failure") == 0) {
            FILE* file = fopen("ux0:data/bluewake/GZLE01.iso", "wb");
            assert(file != NULL && fclose(file) == 0);
            cpu->external_write(cpu, DOL_DI_BASE + DOL_DI_STATUS_OFF, DOL_DI_STATUS_TCINTMASK | DOL_DI_STATUS_TCINT, 4u);
            read_command(host, opcode, physical, offset, length, control);
            assert(host->error != NULL && host->di.status & DOL_DI_STATUS_DEINT);
            assert(!(host->di.status & DOL_DI_STATUS_TCINT));
            assert(host->di.dma_address == physical && host->di.dma_length == length);
        }
    } else {
        assert(host->error != NULL && host->error_pc == 0x80001000u && host->error_address == opcode);
        assert(host->di.status & DOL_DI_STATUS_DEINT);
        assert(!(host->di.status & DOL_DI_STATUS_TCINT));
        assert(host->di.dma_address == physical && host->di.dma_length == length);
        if (strcmp(scenario, "short-read") != 0) {
            for (u32 index = 0u; index < 128u; ++index)
                assert(cpu->ram[0x1FE0u + index] == 0xA5u);
        }
    }
    assert(memcmp(&gather, &host->gather, sizeof(gather)) == 0);
    assert(memcmp(&frontier, &host->frontier, sizeof(frontier)) == 0);
    assert(host->gx.fifo.size == 0u && host->cycles.absolute_cycles == 0u);
    ww_host_shutdown(host);
    assert(!dvd_image_ready());
    free(host);
    return 0;
}
