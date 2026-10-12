#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[wpar]", 6u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    const char* action = argv[1];
    const u32 value = (u32)strtoul(argv[2], NULL, 0);
    const u32 pc = 0x80301578u;
    cpu->pc = pc;
    const bool credit = strncmp(action, "credit-", 7u) == 0;
    const bool read = strcmp(action, "reset") == 0 || strcmp(action, "credit-read") == 0;
    const bool late = strncmp(action, "late-", 5u) == 0;
    const bool data = late || strcmp(action, "bne-partial") == 0 || strcmp(action, "bne-queued") == 0;
    if (data) {
        ww_probe_fifo_setup(host, true);
        ppc_mtspr(cpu, 921u, 0x0C008000u, pc);
        assert(host->error == NULL);
        const bool burst = strcmp(action, "late-burst") == 0 || strcmp(action, "bne-queued") == 0;
        const u8 count = burst ? 4u : 1u;
        for (u8 index = 0u; index < count; ++index)
            mem_write64(cpu, 0xCC008000u, 0x0123456789ABCDEFull);
        assert(host->error == NULL);
    }
    const WwGatherPipe gather = host->gather;
    const WwFifoCapture capture = host->fifo_capture;
    const WwGxCheckpoint frontier = host->frontier;
    const DolInterrupts interrupts = host->interrupts;
    const u32 wp = host->cp_fifo_write_pointer;
    const u32 rp = host->cp_fifo_read_pointer;
    const u32 distance = host->cp_fifo_distance;
    u8 memory[32];
    memcpy(memory, cpu->ram + 0x1000u, sizeof(memory));
    if (credit) {
        bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
        cpu->downcount -= 256;
    }
    if (strcmp(action, "pmc-unsupported") == 0) {
        ppc_mtspr(cpu, 953u, value, pc);
        assert(host->error != NULL && host->error_address == 953u && host->error_pc == pc);
        assert(strcmp(host->error, "unsupported SPR write") == 0);
        assert(ppc_mfspr(cpu, 921u, pc) == 0u);
    } else if (read) {
        assert(ppc_mfspr(cpu, 921u, pc) == 0u && host->error == NULL);
    } else if (strcmp(action, "bne-partial") == 0 || strcmp(action, "bne-queued") == 0) {
        assert(ppc_mfspr(cpu, 921u, pc) == (0x0C008000u | (host->gather.size != 0u ? 1u : 0u)));
        assert(host->error == NULL);
        if (strcmp(action, "bne-queued") == 0)
            assert(host->gather.size == 0u && host->cp_fifo_distance == 32u && host->frontier.size == 0u);
    } else {
        if (strcmp(action, "trace") == 0)
            fail_trace = true;
        if (strcmp(action, "instruction") == 0) {
            cpu->gpr[3] = value;
            const u32 instruction = (31u << 26u) | (3u << 21u) | ((921u & 31u) << 16u) |
                                    ((921u >> 5u) << 11u) | (467u << 1u);
            ppc_fallback_instruction(cpu, instruction, pc);
        } else {
            ppc_mtspr(cpu, 921u, value, pc);
        }
        if (strcmp(action, "reject") == 0 || strcmp(action, "trace") == 0 || late) {
            assert(host->error != NULL && host->error_address == 921u && host->error_pc == pc);
            assert(host->error_width == 4u && host->error_value == value);
            assert(ppc_mfspr(cpu, 921u, pc) == (data ? 0x0C008000u | (gather.size != 0u ? 1u : 0u) : 0u));
        } else {
            assert(host->error == NULL && cpu->exception == 0u);
            assert(ppc_mfspr(cpu, 921u, pc) == value && host->error == NULL);
            if (strcmp(action, "repeat") == 0) {
                ppc_mtspr(cpu, 921u, value, pc);
                assert(ppc_mfspr(cpu, 921u, pc) == value && host->error == NULL);
            }
        }
    }
    assert(memcmp(&gather, &host->gather, sizeof(gather)) == 0);
    assert(memcmp(&capture, &host->fifo_capture, sizeof(capture)) == 0);
    assert(memcmp(&frontier, &host->frontier, sizeof(frontier)) == 0);
    assert(memcmp(&interrupts, &host->interrupts, sizeof(interrupts)) == 0);
    assert(memcmp(memory, cpu->ram + 0x1000u, sizeof(memory)) == 0);
    assert(host->cp_fifo_write_pointer == wp && host->cp_fifo_read_pointer == rp && host->cp_fifo_distance == distance);
    assert(cpu->pc == (strcmp(action, "instruction") == 0 ? pc + 4u : pc));
    assert(host->gx.fifo.size == 0u && !host->frontier.complete);
    assert(host->cycles.absolute_cycles == (credit ? 256u : 0u));
    if (credit) {
        assert(cpu->cycle_budget == 0 && cpu->timebase == 21u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, cpu) == 0u);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
