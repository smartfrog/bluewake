#include "probe_support.h"
#include <stdlib.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    ww_probe_fifo_setup(host, true);
    host->cpu.pc = 0x80321344u;
    const char* scenario = argv[1];
    const bool read = strcmp(scenario, "read") == 0 || strcmp(scenario, "credit-read") == 0 ||
                      strncmp(scenario, "read-width-", 11u) == 0;
    const bool credit = strncmp(scenario, "credit-", 7u) == 0;
    const bool bad_value = strncmp(scenario, "value-", 6u) == 0;
    const bool bad_write = strncmp(scenario, "write-width-", 12u) == 0;
    const bool bad_read = strncmp(scenario, "read-width-", 11u) == 0;
    const bool queued = strncmp(scenario, "queued-", 7u) == 0;
    if (queued) {
        const char* state = scenario + 7u;
        if (strcmp(state, "gather") == 0 || strcmp(state, "produced") == 0) {
            const u8 count = strcmp(state, "produced") == 0 ? 4u : 1u;
            for (u8 index = 0u; index < count; ++index)
                mem_write64(&host->cpu, 0xCC008000u, 0x0123456789ABCDEFull);
            assert(host->error == NULL);
        } else if (strcmp(state, "checkpoint") == 0) {
            assert(ww_gx_write(&host->frontier, &host->gx, 0x10u, 1u, host->cpu.pc, host->cpu.gpr[1]));
        } else if (strcmp(state, "gpread") == 0) {
            host->cp_control |= 1u;
        } else {
            assert(strcmp(state, "cp-irq") == 0);
            dol_interrupts_set_source(&host->interrupts, DOL_PI_CAUSE_CP, true);
        }
    }
    dol_interrupts_set_source(&host->interrupts, DOL_PI_CAUSE_DEBUG, true);
    host->interrupts.pi_mask |= DOL_PI_CAUSE_DEBUG;
    const DolInterrupts interrupts = host->interrupts;
    const WwGatherPipe gather = host->gather;
    const WwGxCheckpoint frontier = host->frontier;
    u8 memory[192];
    memcpy(memory, host->cpu.ram + 0xFE0u, sizeof(memory));
    const u32 pi = host->pi_fifo_write_pointer;
    const u32 cp = host->cp_fifo_write_pointer;
    const u32 distance = host->cp_fifo_distance;
    const u16 control = host->cp_control;
    const u64 value = bad_value ? strtoull(scenario + 6u, NULL, 0) : 3u;
    const u8 width = bad_write ? (u8)strtoul(scenario + 12u, NULL, 0) :
                     bad_read ? (u8)strtoul(scenario + 11u, NULL, 0) : 2u;
    if (credit) {
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount -= 256;
    }
    if (read) {
        const u64 result = bad_read ? host->cpu.external_read(&host->cpu, 0xCC000004u, width) :
                                     mem_read16(&host->cpu, 0xCC000004u);
        assert(result == 0u);
    } else if (strcmp(scenario, "store") == 0) {
        host->cpu.gpr[3] = 0xCC000000u;
        host->cpu.gpr[0] = 3u;
        mem_write16(&host->cpu, host->cpu.gpr[3] + 4u, (u16)host->cpu.gpr[0]);
    } else {
        host->cpu.external_write(&host->cpu, 0xCC000004u, value, width);
    }
    if (bad_value || bad_write || bad_read || queued) {
        assert(host->error != NULL && host->error_address == 0xCC000004u && host->error_pc == 0x80321344u);
        if (!read)
            assert(host->error_width == width && host->error_value == value);
    } else {
        assert(host->error == NULL && host->cpu.exception == 0u);
        if (strcmp(scenario, "repeat") == 0)
            mem_write16(&host->cpu, 0xCC000004u, 3u);
        assert(mem_read16(&host->cpu, 0xCC000004u) == 0u && host->error == NULL);
    }
    assert(memcmp(&interrupts, &host->interrupts, sizeof(interrupts)) == 0);
    assert(memcmp(&gather, &host->gather, sizeof(gather)) == 0);
    assert(memcmp(&frontier, &host->frontier, sizeof(frontier)) == 0);
    assert(memcmp(memory, host->cpu.ram + 0xFE0u, sizeof(memory)) == 0);
    assert(host->pi_fifo_write_pointer == pi && host->cp_fifo_write_pointer == cp && host->cp_fifo_distance == distance);
    assert(host->cp_fifo_read_pointer == 0x1000u && host->cp_control == control);
    assert(host->pi_fifo_base == 0x1000u && host->cp_fifo_base == 0x1000u);
    assert(host->pi_fifo_end == 0x1060u && host->cp_fifo_end == 0x1060u);
    assert(host->cpu.pc == 0x80321344u && host->gx.fifo.size == 0u);
    assert(host->cycles.absolute_cycles == (credit ? 256u : 0u));
    if (credit) {
        assert(host->cpu.cycle_budget == 0 && host->cpu.timebase == 21u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
